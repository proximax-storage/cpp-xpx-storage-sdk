#include "drive/Session.h"
#include "drive/Kademlia.h"
#include "KademliaBucket.h"
#include "KademliaNode.h"
#include "drive/Timer.h"

namespace sirius { namespace drive { namespace kademlia {

using NodeInfo = ReplicatorInfo;
using namespace sirius::drive;

class PeerSearcher
{
    PeerKey                 m_targetPeerKey;
    std::vector<NodeInfo>   m_candidates;
    std::set<PeerKey>       m_loserPeers;

    EndpointCatalogue&      m_endpointCatalogue;
    std::weak_ptr<Session>  m_session;
    Timer                   m_timer;

    const int PEER_ASWER_LIMIT_MS = 1000;

public:

    PeerSearcher( const PeerKey&                  targetPeerKey,
               const std::vector<PeerInfo>&     candidates,
               EndpointCatalogue&               endpointCatalogue,
               std::weak_ptr<Session>           session )
    :
        m_targetPeerKey(targetPeerKey),
        m_endpointCatalogue(endpointCatalogue),
        m_session(session)
    {
        for( auto& peerInfo : candidates )
        {
            m_candidates.emplace_back( NodeInfo{ peerInfo.endpoint(), peerInfo.m_publicKey } );
        }

        if ( auto session = m_session.lock(); session )
        {
            PeerIpRequest request{ session->isClient(), targetPeerKey, m_endpointCatalogue.publicKey() };
            //TODO? candidates[0].m_endpoint should be replaced by nearest node
            session->sendGetPeerIpRequest( request, m_candidates[0].m_endpoint );
            m_timer = session->startTimer( PEER_ASWER_LIMIT_MS, []{} );
        }
    }

    void onGetMyIpResponse( const std::string& )
    {
    }
};

class EndpointCatalogueImpl : public EndpointCatalogue
{
    using SearcherMap = std::map<PeerKey,std::unique_ptr<PeerSearcher>>;

    std::weak_ptr<Session>          m_kademliaTransport;
    const crypto::KeyPair&          m_keyPair;
    std::vector<NodeInfo>           m_bootstraps;
    uint8_t                         m_myPort;
    bool                            m_isClient;
    
    std::optional<PeerInfo>         m_myPeerInfo;

    std::map<PeerKey,boost::asio::ip::udp::endpoint> m_localEndpointMap;

    HashTable                      m_hashTable;
    SearcherMap                    m_searcherMap;

private:
    boost::asio::ip::udp::endpoint  m_myIp;

public:

    EndpointCatalogueImpl(  std::weak_ptr<Session>        kademliaTransport,
                            const crypto::KeyPair&        keyPair,
                            const std::vector<NodeInfo>&  bootstraps,
                            uint8_t                       myPort,
                            bool                          isClient )
        :   m_kademliaTransport(kademliaTransport),
            m_keyPair(keyPair),
            m_bootstraps(bootstraps),
            m_myPort(myPort),
            m_isClient(isClient)
    {
        std::erase_if( m_bootstraps, [this]( const auto& item )
        {
            return m_keyPair.publicKey().array() == item.m_publicKey;
        });

        for( const auto& nodeInfo : m_bootstraps )
        {
            m_localEndpointMap[nodeInfo.m_publicKey.array()] = nodeInfo.m_endpoint;
        }
    }

    virtual PeerKey publicKey() override { return m_keyPair.publicKey().array(); }

    std::optional<boost::asio::ip::udp::endpoint> getEndpoint( PeerKey& key ) override
    {
        // find in local map (usually replicators of common drives)
        //
        if ( auto it = m_localEndpointMap.find(key); it != m_localEndpointMap.end() )
        {
            return it->second;
        }

        // find in Kademlia hash table
        Bucket* bucket;
        if ( auto peerInfo = m_hashTable.getPeerInfo( key, bucket ); peerInfo )
        {
            return peerInfo;
        }

        // start searching
        if ( bucket != nullptr )
        {
            if ( auto it = m_searcherMap.find( key ); it == m_searcherMap.end() )
            {
                m_searcherMap[key] = std::make_unique<PeerSearcher>( key, bucket->nodes(), *this, m_kademliaTransport );
            }
        }
        
        // not found yet
        return {};
    }

    std::string onGetMyIpRequest( const std::string& request, boost::asio::ip::udp::endpoint requesterEndpoint ) override
    {
        try
        {
            std::istringstream is( request, std::ios::binary );
            cereal::PortableBinaryInputArchive iarchive(is);
            uint16_t port;
            iarchive( port );
            
            if ( port == requesterEndpoint.port() )
            {
                MyIpResponse response{ m_keyPair, requesterEndpoint };
                std::ostringstream os( std::ios::binary );
                cereal::PortableBinaryOutputArchive iarchive(os);
                iarchive( response );
                return os.str();
            }
            else
            {
                __LOG_WARN( "onGetMyIpRequest: bad port: " << port << " != " << requesterEndpoint.port() )
            }
        } catch (...) {
            __LOG_WARN( "exception in onGetMyIpRequest" )
        }
        return "";
    }

    void onGetMyIpResponse( const std::string& responseStr ) override
    {
        try
        {
            std::istringstream is( responseStr, std::ios::binary );
            cereal::PortableBinaryInputArchive archive(is);
            MyIpResponse response;
            archive( response );
            if ( ! response.verify() )
            {
                // ignore
                __LOG_WARN( "ignore bad MyIpResponse" )
                return;
            }
            
            if ( m_myPort != response.m_response.m_port )
            {
                __LOG_WARN( "ignore bad MyIpResponse" )
                return;
            }

            bool firstResponse = !m_myPeerInfo.has_value();
            m_myPeerInfo = PeerInfo{ m_keyPair.publicKey().array(), response.m_response.endpoint() };
            m_myPeerInfo->Sign( m_keyPair );
            
            if ( firstResponse )
            {
                enterToSwarm();
            }
            
        } catch (...) {
            __LOG_WARN( "exception in onGetMyIpResponse" )
        }
    }

    std::string onGetPeerIpRequest( const std::string& ) override
    {
        return "";
    }

    void            onGetPeerIpResponse( const std::string& ) override
    {
    }
    
    void enterToSwarm()
    {
        PeerKey searchedKey = m_keyPair.publicKey().array();
        //TODO? maybe searchedKey[31] = searchedKey[0] ^ 0x01;
        searchedKey[0] = searchedKey[0] ^ 0x01;
        
        PeerIpRequest request{ m_isClient, m_keyPair.publicKey().array(), searchedKey };
        if ( auto session = m_kademliaTransport.lock(); session )
        {
            for( auto bootstrapNode: m_bootstraps )
            {
                session->sendGetPeerIpRequest( request, bootstrapNode.m_endpoint );
            }
        }
    }
};



} // namespace kademlia

std::unique_ptr<kademlia::EndpointCatalogue> createEndpointCatalogue(
                                                             std::weak_ptr<Session>             kademliaTransport,
                                                             const crypto::KeyPair&             keyPair,
                                                             const std::vector<ReplicatorInfo>& bootstraps,
                                                             uint8_t                            myPort,
                                                             bool                               isClient )
{
    return std::make_unique<kademlia::EndpointCatalogueImpl>( kademliaTransport,
                                                              keyPair,
                                                              bootstraps,
                                                              myPort,
                                                              isClient );
}

}}



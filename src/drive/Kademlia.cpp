#include "drive/Session.h"
#include "drive/Kademlia.h"
#include "KademliaBucket.h"
#include "KademliaHashTable.h"
#include "drive/Timer.h"
#include "drive/Utils.h"

#include <cereal/types/vector.hpp>
#include <cereal/types/array.hpp>
#include <cereal/types/map.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/archives/portable_binary.hpp>

namespace sirius { namespace drive { namespace kademlia {

using NodeInfo = ReplicatorInfo;
using namespace ::sirius::drive;

class EndpointCatalogueImpl;
class PeerSearchInfo;
void  onGetPeerIpResponseWrap( PeerSearchInfo& searchInfo, PeerIpResponse& response );

PeerSearchInfo createPeerSearchInfo(   const PeerKey&                  myPeerKey,
                                       const PeerKey&                  targetPeerKey,
                                       size_t                          bucketIndex,
                                       EndpointCatalogueImpl&          endpointCatalogue,
                                       std::weak_ptr<Session>          session );

class EndpointCatalogueImpl : public EndpointCatalogue
{
public:
    
    using SearcherMap = std::map<PeerKey,std::unique_ptr<PeerSearchInfo>>;

    std::weak_ptr<Session>          m_kademliaTransport;
    const crypto::KeyPair&          m_keyPair;
    std::vector<NodeInfo>           m_bootstraps;
    uint8_t                         m_myPort;
    bool                            m_isClient;
    
    std::optional<PeerInfo>         m_myPeerInfo;

    std::map<PeerKey,std::optional<boost::asio::ip::udp::endpoint>> m_localEndpointMap;

    KademliaHashTable              m_hashTable;
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
            return m_keyPair.publicKey() == item.m_publicKey;
        });
        _SIRIUS_ASSERT( m_bootstraps.size() > 0 );

        for( const auto& nodeInfo : m_bootstraps )
        {
            m_localEndpointMap[nodeInfo.m_publicKey.array()] = nodeInfo.m_endpoint;
        }
    }

    virtual void stop() override
    {
        //TODO? remove timers
    }

    virtual PeerKey publicKey() override { return m_keyPair.publicKey().array(); }

    void addClientToLocalEndpointMap( const Key& key ) override
    {
        m_localEndpointMap[key] = {};
    }

    virtual void onEndpointDiscovered( const Key& key, const std::optional<boost::asio::ip::udp::endpoint>& endpoint ) override
    {
        if ( auto it = m_localEndpointMap.find(key); it != m_localEndpointMap.end() )
        {
            it->second = endpoint;
        }
    }

    // getEndpoint() for local using only
    //
    std::optional<boost::asio::ip::udp::endpoint> getEndpoint( const PeerKey& key ) override
    {
        // find in local map (usually replicators of common drives)
        //
        if ( auto it = m_localEndpointMap.find(key); it != m_localEndpointMap.end() )
        {
            return it->second;
        }

        // find in Kademlia hash table
        size_t bucketIndex;
        if ( auto peerInfo = m_hashTable.getPeerInfo( key, bucketIndex ); peerInfo )
        {
            return peerInfo;
        }

        // start searching (skip if already started)
        if ( auto it = m_searcherMap.find( key ); it == m_searcherMap.end() )
        {
            m_searcherMap[key] = std::make_unique<PeerSearchInfo>( this->m_keyPair.publicKey().array(),
                                                                  key,
                                                                  bucketIndex,
                                                                  *this,
                                                                  m_kademliaTransport );
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

    std::string onGetPeerIpRequest( const std::string& requestStr, boost::asio::ip::udp::endpoint requesterEndpoint ) override
    {
        try
        {
            std::istringstream is( requestStr, std::ios::binary );
            cereal::PortableBinaryInputArchive archive(is);
            PeerIpRequest request;
            archive( request );
            
            if ( m_hashTable.couldBeAdded( request.m_requesterKey ) )
            {
                if ( auto session = m_kademliaTransport.lock(); session )
                {
                    session->sendGetPeerIpRequest( PeerIpRequest{ false, request.m_requesterKey, publicKey() }, requesterEndpoint );
                }
            }
            
            // find in Kademlia hash table
            std::vector<PeerInfo> peers = m_hashTable.onRequestFromAnotherPeer( request.m_peerKey );
            PeerIpResponse response{ request.m_peerKey, peers };

            std::ostringstream os( std::ios::binary );
            cereal::PortableBinaryOutputArchive iarchive(os);
            iarchive( response );
            return os.str();
        }
        catch(...) {
            __LOG_WARN( "exception in onGetPeerIpRequest" )
        }

        return "";
    }

    void onGetPeerIpResponse( const std::string& responseStr ) override
    {
        try
        {
            std::istringstream is( responseStr, std::ios::binary );
            cereal::PortableBinaryInputArchive archive(is);
            PeerIpResponse response;
            archive( response );
            
            if ( response.m_response.size() == 0 )
            {
                return;
            }
            
            for( auto& peerInfo : response.m_response )
            {
                if ( ! peerInfo.Verify() )
                {
                    __LOG_WARN( "onGetPeerIpResponse: bad sign" );
                    return;
                }
            }
            
            if ( auto it = m_searcherMap.find(response.m_peerKey); it != m_searcherMap.end() )
            {
                kademlia::onGetPeerIpResponseWrap( *it->second, response );
            }
            else
            {
                __LOG( "onGetPeerIpResponse: old response?");
            }
        }
        catch(...) {
            __LOG_WARN( "exception in onGetPeerIpResponse" )
        }
    }
    
    void enterToSwarm()
    {
        PeerKey searchedKey = m_keyPair.publicKey().array();
        //TODO? maybe searchedKey[0] = searchedKey[0] ^ 0x01;
        searchedKey[0] = searchedKey[31] ^ 0x01;
        
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

class PeerSearchInfo
{
    struct Candidate
    {
        boost::asio::ip::udp::endpoint  m_endpoint;
        PeerKey                         m_publicKey;
        PeerKey                         m_xorValue;
        
        bool operator<( const Candidate& item ) const {
            return m_xorValue<item.m_xorValue;
        }
    };
    
    PeerKey                 m_myPeerKey;
    PeerKey                 m_targetPeerKey;
    size_t                  m_bucketIndex;
    std::vector<Candidate>  m_candidates;
    std::set<PeerKey>       m_triedPeers;

    EndpointCatalogueImpl&  m_endpointCatalogue;
    std::weak_ptr<Session>  m_session;
    Timer                   m_timer;

    const int PEER_ASWER_LIMIT_MS = 1000;

public:

    PeerSearchInfo( const PeerSearchInfo& ) = default;

    PeerSearchInfo( const PeerKey&                 myPeerKey,
                   const PeerKey&                  targetPeerKey,
                   size_t                          bucketIndex,
                   EndpointCatalogueImpl&          endpointCatalogue,
                   std::weak_ptr<Session>          session )
    :
        m_myPeerKey(myPeerKey),
        m_targetPeerKey(targetPeerKey),
        m_bucketIndex(bucketIndex),
        m_endpointCatalogue(endpointCatalogue),
        m_session(session)
    {
        for(;;)
        {
            for( const auto& peerInfo : m_endpointCatalogue.m_hashTable.buckets()[bucketIndex].nodes() )
            {
                m_candidates.emplace_back( Candidate{ peerInfo.endpoint(), peerInfo.m_publicKey, xorValue(peerInfo.m_publicKey, m_myPeerKey) } );
            }
            if ( m_candidates.empty() )
            {
                if ( m_bucketIndex==0 )
                {
                    const auto& bootstrapNode = m_endpointCatalogue.m_bootstraps[0];
                    m_candidates.emplace_back( Candidate{ bootstrapNode.m_endpoint,
                                                        bootstrapNode.m_publicKey.array(),
                                                        xorValue(bootstrapNode.m_publicKey, m_myPeerKey) } );
                    break;
                }
                m_bucketIndex--;
            }
        }
        
        std::sort( m_candidates.begin(), m_candidates.end() );

        sendNextRequest();
    }
    
    inline void sendNextRequest()
    {
        if ( auto session = m_session.lock(); session )
        {
            PeerIpRequest request{ session->isClient(), m_targetPeerKey, m_endpointCatalogue.publicKey() };
            session->sendGetPeerIpRequest( request, m_candidates.back().m_endpoint );
            m_triedPeers.insert( m_candidates.back().m_publicKey );
            m_candidates.pop_back();

            m_timer = session->startTimer( PEER_ASWER_LIMIT_MS, [this]{ onTimer(); } );
        }
    }
    
    void onTimer()
    {
        if ( m_candidates.empty() )
        {
            __LOG( "onTimer: no candidates" )
            return;
        }
        
        sendNextRequest();
    }

    // Response from another peer
    void onGetPeerIpResponse( const PeerIpResponse& response )
    {
        if ( response.m_response.size() == 1 && response.m_response[0].m_publicKey == m_targetPeerKey )
        {
            const PeerInfo& peerInfo = response.m_response[0];
            
            if ( ! peerInfo.Verify() )
            {
                __LOG_WARN( "PeerSearchInfo::onGetPeerIpResponse: bad sign: " << peerInfo.m_publicKey )
                sendNextRequest();
                return;
            }
            
            if ( isPeerInfoExpired(peerInfo.m_timeInSeconds) )
            {
                __LOG_WARN( "PeerSearchInfo::onGetPeerIpResponse: expired: " << peerInfo.m_publicKey )
                sendNextRequest();
                return;
            }
            
            m_endpointCatalogue.m_localEndpointMap[ peerInfo.m_publicKey ] = peerInfo.endpoint();

            m_endpointCatalogue.m_hashTable.addPeerInfo( response.m_response[0] );
            return;
        }
        
        for( const auto& peerInfo: response.m_response )
        {
            if ( ! peerInfo.Verify() )
            {
                __LOG_WARN( "PeerSearchInfo::onGetPeerIpResponse: bad sign(2): " << peerInfo.m_publicKey )
                continue;
            }
            
//?               if ( isPeerInfoExpired(peerInfo.m_timeInSeconds) )
//                {
//                    __LOG_WARN( "PeerSearchInfo::onGetPeerIpResponse: expired(2): " << toString(peerInfo.m_publicKey) )
//                    continue;
//                }

            if ( auto it = m_triedPeers.find( peerInfo.m_publicKey ); it != m_triedPeers.end() )
            {
                continue;
            }
            
            const auto it = std::find_if( m_candidates.begin(), m_candidates.end(), [&key=peerInfo.m_publicKey](const auto& item)
            {
                return item.m_publicKey == key;
            });
            if ( it != m_candidates.end() )
            {
                continue;
            }
            
            m_candidates.emplace_back( Candidate{ peerInfo.endpoint(),
                peerInfo.m_publicKey,
                peerInfo.m_publicKey ^ m_myPeerKey } );
        }

        std::sort( m_candidates.begin(), m_candidates.end() );

        sendNextRequest();
    }
};

inline PeerSearchInfo createPeerSearchInfo( const PeerKey&                  myPeerKey,
                                            const PeerKey&                  targetPeerKey,
                                            size_t                          bucketIndex,
                                            EndpointCatalogueImpl&          endpointCatalogue,
                                            std::weak_ptr<Session>          session )
{
    return PeerSearchInfo{ myPeerKey,
                          targetPeerKey,
                          bucketIndex,
                          endpointCatalogue,
                          session };
}

inline void onGetPeerIpResponseWrap( PeerSearchInfo& searchInfo, PeerIpResponse& response )
{
    searchInfo.onGetPeerIpResponse( response );
}

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



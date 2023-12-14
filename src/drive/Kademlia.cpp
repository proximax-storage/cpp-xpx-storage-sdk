//TODO
// - Filter by m_registeredReplicators (with queue of unknowns/leading ones)
// - client skip requests and ???
// - simultaneous requesting
// - update timer -> sendGetPeerIpRequest(of myIp)
// ___LOG
// //TODO?


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

//todo?
#define _KADEMLIA_LOCAL_TEST_

const int PEER_ANSWER_LIMIT_MS = 2000;


namespace sirius { namespace drive { namespace kademlia {

using NodeInfo = ReplicatorInfo;
using namespace ::sirius::drive;

const int MAX_ATTEMPT_NUMBER = 3;

class EndpointCatalogueImpl;
class PeerSearchInfo;

using OptionalEndpoint = OptionalEndpoint;

bool  onGetPeerIpResponseRedirect( PeerSearchInfo& searchInfo, PeerIpResponse& response );

inline std::unique_ptr<PeerSearchInfo> createPeerSearchInfo( const PeerKey&        myPeerKey,
                                                   const PeerKey&                  targetPeerKey,
                                                   size_t                          bucketIndex,
                                                   OptionalEndpoint                directEndpoint,
                                                   EndpointCatalogueImpl&          endpointCatalogue,
                                                   std::weak_ptr<Session>          session );

class EndpointCatalogueImpl : public EndpointCatalogue
{
public:
    
    using SearcherMap = std::map<PeerKey,std::unique_ptr<PeerSearchInfo>>;

    std::weak_ptr<Session>          m_kademliaTransport;
    const crypto::KeyPair&          m_keyPair;
    std::vector<NodeInfo>           m_bootstraps;
    uint16_t                        m_myPort;
    bool                            m_isClient;
    
    std::optional<PeerInfo>         m_myPeerInfo;
    Timer                           m_myPeerInfoTimer;
    
    std::map<PeerKey,OptionalEndpoint> m_localEndpointMap;

    KademliaHashTable               m_hashTable;
    SearcherMap                     m_searcherMap;
    
    std::set<const Key>             m_registeredReplicators;
    
    std::optional<::sirius::drive::EndpointHandler> m_endpointHandler;

private:
    OptionalEndpoint                m_myEndpoint;

public:

    EndpointCatalogueImpl(  std::weak_ptr<Session>        kademliaTransport,
                            const crypto::KeyPair&        keyPair,
                            const std::vector<NodeInfo>&  bootstraps,
                            uint16_t                      myPort,
                            bool                          isClient )
        :   m_kademliaTransport(kademliaTransport),
            m_keyPair(keyPair),
            m_bootstraps(bootstraps),
            m_myPort(myPort),
            m_isClient(isClient),
            m_hashTable( keyPair.publicKey() )
    {
        auto it = std::find_if( m_bootstraps.begin(), m_bootstraps.end(), [this]( const auto& item )
        {
            return m_keyPair.publicKey() == item.m_publicKey;
        });
                               
        if ( it != m_bootstraps.end() )
        {
//            m_myPeerInfo = PeerInfo{ m_keyPair.publicKey(), it->m_endpoint };
//            m_myPeerInfo->Sign( m_keyPair );
            m_myEndpoint = it->m_endpoint;
            m_bootstraps.erase( it );
        }
        
        std::erase_if( m_bootstraps, [this]( const auto& item )
        {
            return m_keyPair.publicKey() == item.m_publicKey;
        });
        _SIRIUS_ASSERT( m_bootstraps.size() > 0 );

        for( const auto& nodeInfo : m_bootstraps )
        {
            m_localEndpointMap[nodeInfo.m_publicKey] = nodeInfo.m_endpoint;
        }
        
        // make some delay (for starting dht)
        if ( auto session = m_kademliaTransport.lock(); session )
        {
            start();
        }
    }
    
    ~EndpointCatalogueImpl()
    {
        m_myPeerInfoTimer.cancel();
    }

    void start()
    {
        ___LOG( m_myPort << " : start: " << "  " << m_bootstraps.size() )
        for( const auto& bootstrapNodeInfo : m_bootstraps )
        {
            if ( auto kademliaTransport = m_kademliaTransport.lock(); kademliaTransport )
            {
                MyIpRequest request{m_myPort};
                
                kademliaTransport->sendGetMyIpRequest( request, bootstrapNodeInfo.m_endpoint );
            }
        }
        
        if ( auto session = m_kademliaTransport.lock(); session )
        {
            m_myPeerInfoTimer = session->startTimer( PEER_ANSWER_LIMIT_MS, [this]{ onMyPeerInfoTimer(); } );
        }
    }
    
    void onMyPeerInfoTimer()
    {
        ___LOG( m_myPort << " : onMyPeerInfoTimer:" )
        
        // Try again
        for( const auto& bootstrapNodeInfo : m_bootstraps )
        {
            if ( auto kademliaTransport = m_kademliaTransport.lock(); kademliaTransport )
            {
                MyIpRequest request{m_myPort};
                
                kademliaTransport->sendGetMyIpRequest( request, bootstrapNodeInfo.m_endpoint );
            }
        }
        if ( auto session = m_kademliaTransport.lock(); session )
        {
            m_myPeerInfoTimer = session->startTimer( PEER_ANSWER_LIMIT_MS, [this]{ onMyPeerInfoTimer(); } );
        }
    }

    virtual void stopTimers() override
    {
        m_myPeerInfoTimer.cancel();
    }

    virtual PeerKey publicKey() override { return m_keyPair.publicKey(); }

    void addClientToLocalEndpointMap( const Key& key ) override
    {
        m_localEndpointMap[key] = {};
    }

    virtual void onEndpointDiscovered( const Key& key, const OptionalEndpoint& endpoint ) override
    {
        if ( auto it = m_localEndpointMap.find(key); it != m_localEndpointMap.end() )
        {
            it->second = endpoint;
        }
        (*m_endpointHandler)( key, endpoint );
    }
    
    virtual void setEndpointHandler( ::sirius::drive::EndpointHandler endpointHandler ) override
    {
        m_endpointHandler = endpointHandler;
    }

    OptionalEndpoint getEndpoint( const PeerKey& key ) override
    {
        return queryPeerInfo( key, {} );
    }
    
    OptionalEndpoint queryPeerInfo( const PeerKey& key, OptionalEndpoint directEndpoint )
    {
        // find in local map (usually replicators of common drives)
        //
        if ( ! directEndpoint )
        {
            if ( auto it = m_localEndpointMap.find(key); it != m_localEndpointMap.end() )
            {
                return it->second;
            }
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
            //___LOG( "request: " << m_myPort << ": " << key )
            m_searcherMap[key] = createPeerSearchInfo( this->m_keyPair.publicKey(),
                                                      key,
                                                      bucketIndex,
                                                      directEndpoint,
                                                      *this,
                                                      m_kademliaTransport );
        }
        
        // not found yet
        return {};
    }

    std::string onGetMyIpRequest( const std::string& request, boost::asio::ip::udp::endpoint requesterEndpoint ) override
    {
        ___LOG( "onGetMyIpRequest: from: " << requesterEndpoint << " to: " << m_myPort )
        try
        {
            std::istringstream is( request, std::ios::binary );
            cereal::PortableBinaryInputArchive iarchive(is);
            MyIpRequest request;
            iarchive( request );
            
            if ( request.m_myPort == requesterEndpoint.port() )
            {
                MyIpResponse response{ m_keyPair, requesterEndpoint };
                std::ostringstream os( std::ios::binary );
                cereal::PortableBinaryOutputArchive iarchive(os);
                iarchive( response );
                return os.str();
            }
            else
            {
                __LOG_WARN( "onGetMyIpRequest: bad port: " << request.m_myPort << " != " << requesterEndpoint.port() )
            }
        } catch (...) {
            __LOG_WARN( "exception in onGetMyIpRequest" )
        }
        return "";
    }

    void onGetMyIpResponse( const std::string& responseStr, boost::asio::ip::udp::endpoint responserEndpoint ) override
    {
        __LOG( "onGetMyIpResponse: " )
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
            m_myPeerInfo = PeerInfo{ m_keyPair.publicKey(), response.m_response.endpoint() };
            m_myPeerInfo->Sign( m_keyPair );
            m_myPeerInfoTimer.cancel();
            
            if ( m_myEndpoint && response.m_response.endpoint().port() != m_myEndpoint->port() )
            {
                __LOG_WARN( "Invalid replicators.json! wrong my port number! (invalid bootstrap list): " << response.m_response.endpoint() << " vs " << *m_myEndpoint )
            }
            
            if ( m_myEndpoint && response.m_response.endpoint().address() != m_myEndpoint->address() )
            {
                __LOG_WARN( "Invalid replicators.json! wrong my address number! (invalid bootstrap list): " << response.m_response.endpoint() << " vs " << *m_myEndpoint )
            }
            
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
        __LOG( "onGetPeerIpRequest: " )

        try
        {
            std::istringstream is( requestStr, std::ios::binary );
            cereal::PortableBinaryInputArchive archive(is);
            PeerIpRequest request;
            archive( request );
            
            if ( request.m_requesterKey != m_keyPair.publicKey() && m_hashTable.couldBeAdded( request.m_requesterKey ) )
            {
                // Query requester peerInfo if it could be added to my hashtable
                queryPeerInfo( request.m_requesterKey, requesterEndpoint );
            }
            
            std::vector<PeerInfo> peers;
            
            // Is it my peer?
            if ( request.m_peerKey == m_keyPair.publicKey() )
            {
                if ( requesterEndpoint.port() == 5003 )
                {
                    ___LOG( "<5003 onGetPeerIpRequest: " << m_myPort << " " << request.m_peerKey )
                }

                if ( !m_myPeerInfo )
                {
                    if ( requesterEndpoint.port() == 5003 )
                    {
                        ___LOG( "<5003 onGetPeerIpRequest: !m_myPeerInfo " << m_myPort )
                    }
                    return "";
                }
                
                if ( requesterEndpoint.port() == 5003 )
                {
                    ___LOG("5003 response: " << m_myPort << " responser: " << requesterEndpoint.port() )
                }
                m_myPeerInfo->updateCreationTime( m_keyPair );
                peers.push_back(*m_myPeerInfo);
            }
            else
            {
                // find in Kademlia hash table
                peers  = m_hashTable.onRequestFromAnotherPeer( request.m_peerKey );
            }
            
            // return response
            PeerIpResponse response{ request.m_peerKey, peers };
            if ( requesterEndpoint.port() == 5003 )
            {
                ___LOG( "5003 PeerIpResponse: " << m_myPort << " tg: " << request.m_peerKey << " requester: " << requesterEndpoint.port() )
            }

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

    void onGetPeerIpResponse( const std::string& responseStr, boost::asio::ip::udp::endpoint responserEndpoint ) override
    {
        __LOG( "onGetPeerIpResponse: " )
        
        try
        {
            // Unpack response
            std::istringstream is( responseStr, std::ios::binary );
            cereal::PortableBinaryInputArchive archive(is);
            PeerIpResponse response;
            archive( response );
            
            if ( m_myPort == 5003 )
            {
                ___LOG("5003 onGetPeerIpResponse (received):" << m_myPort << " responser: " << responserEndpoint.port()  << " target: " << response.m_peerKey )
            }

            if ( response.m_response.size() == 0 )
            {
                // ignore empty response
                if ( m_myPort == 5003 )
                {
                    ___LOG("5003 empty response!!! (received):" << m_myPort << " responser: " << responserEndpoint.port()  << " target: " << response.m_peerKey )
                }
                return;
            }
            
            // Redirect response to searcher
            if ( auto it = m_searcherMap.find(response.m_peerKey); it != m_searcherMap.end() )
            {
                if ( ! kademlia::onGetPeerIpResponseRedirect( *it->second, response ) )
                {
                    m_searcherMap.erase(it);
                    if ( m_myPort == 5003 )
                    {
                        ___LOG("5003 onGetPeerIpResponse found: " << m_myPort << " responser: " << responserEndpoint.port()  << " " << response.m_peerKey )
                    }
                }
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
        // Query peerInfo of bootstraps
        if ( auto session = m_kademliaTransport.lock(); session )
        {
            for( auto nodeInfo: m_bootstraps )
            {
                queryPeerInfo( nodeInfo.m_publicKey, nodeInfo.m_endpoint );
            }
        }

        //TODO?
        return;
        
        PeerKey searchedKey = m_keyPair.publicKey();
        //TODO? maybe searchedKey[0] = searchedKey[0] ^ 0x01;
        searchedKey[0] = searchedKey[31] ^ 0x01;
        
        PeerIpRequest request{ m_isClient, searchedKey, m_keyPair.publicKey() };
        if ( auto session = m_kademliaTransport.lock(); session )
        {
            for( auto bootstrapNode: m_bootstraps )
            {
                if ( m_myPort == 5003 )
                {
                    ___LOG( "5003 Request(2): " << searchedKey )
                }

                session->sendGetPeerIpRequest( request, bootstrapNode.m_endpoint );
            }
        }
    }
    
    virtual void    addReplicatorKey( const Key& key ) override
    {
        m_registeredReplicators.insert(key);
    }
    virtual void    addReplicatorKeys( const std::vector<Key>& keys ) override
    {
        for( const auto& key : keys )
        {
            m_registeredReplicators.insert(key);
        }
    }
    virtual void    removeReplicatorKey( const Key& key ) override
    {
        m_registeredReplicators.erase(key);
        
        if ( auto it = m_localEndpointMap.find(key); it != m_localEndpointMap.end() )
        {
            m_localEndpointMap.erase(key);
        }
        
        m_hashTable.removePeerInfo(key);
    }
    
    virtual void dbgTestKademlia( KademliaDbgFunc dbgFunc ) override
    {
        if ( auto kademliaTransport = m_kademliaTransport.lock(); kademliaTransport )
        {
            boost::asio::post( kademliaTransport->lt_session().get_context(), [=, this]() mutable
            {
                KademliaDbgInfo dbgInfo;
                for ( auto& [key,searchInfo] : m_searcherMap )
                {
                    ___LOG( "m_searcherMap " << m_myPort << ": " << key );
                }
//                dbgInfo.m_requestCounter = m_searcherMap.size();
//                for( const auto& bucket : m_hashTable.buckets() )
//                {
//                    dbgInfo.m_peerCounter += bucket.nodes().size();
//                }
//                ___LOG( "m_myPort: " << m_myPort << " dbgInfo.m_peerCounter: " << dbgInfo.m_peerCounter );
                dbgFunc( dbgInfo );
            });
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
    
    // It is used when we know peer IP, but we need peerInfo (i.e. for updating peerInfo)
    OptionalEndpoint        m_directEndpoint;

    EndpointCatalogueImpl&  m_endpointCatalogue;
    std::weak_ptr<Session>  m_session;

    std::vector<Candidate>  m_candidates;
    std::set<PeerKey>       m_triedPeers;
    Timer                   m_timer;
    int                     m_attemptCounter = 0;

public:

    PeerSearchInfo( const PeerSearchInfo& ) = default;

    PeerSearchInfo( const PeerKey&                 myPeerKey,
                   const PeerKey&                  targetPeerKey,
                   size_t                          bucketIndex,
                   OptionalEndpoint                directEndpoint,
                   EndpointCatalogueImpl&          endpointCatalogue,
                   std::weak_ptr<Session>          session )
    :
        m_myPeerKey(myPeerKey),
        m_targetPeerKey(targetPeerKey),
        m_bucketIndex(bucketIndex),
        m_directEndpoint(directEndpoint),
        m_endpointCatalogue(endpointCatalogue),
        m_session(session)
    {
        if ( m_directEndpoint )
        {
            m_candidates.emplace_back( Candidate{   *m_directEndpoint,
                                                    targetPeerKey,
                                                    xorValue(targetPeerKey, m_myPeerKey) } );
        }
        else
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
                        for( const auto& bootstrapNode : m_endpointCatalogue.m_bootstraps )
                        {
                            m_candidates.emplace_back( Candidate{   bootstrapNode.m_endpoint,
                                bootstrapNode.m_publicKey,
                                xorValue(bootstrapNode.m_publicKey, m_myPeerKey) } );
                        }
                        break;
                    }
                    m_bucketIndex--;
                }
            }
            
            // Sort candidates
            std::sort( m_candidates.begin(), m_candidates.end() );
        }
        
        sendNextRequest();
    }

    ~PeerSearchInfo()
    {
        if ( m_endpointCatalogue.m_myPort == 5003 )
        {
            ___LOG("5003 ~PeerSearchInfo: " << m_targetPeerKey << " in: " << m_endpointCatalogue.m_myPort )
        }
        m_timer.cancel();
    }

    inline void sendNextRequest()
    {
        if ( auto session = m_session.lock(); session )
        {
            PeerIpRequest request{ session->isClient(), m_targetPeerKey, m_endpointCatalogue.publicKey() };
            if ( m_endpointCatalogue.m_myPort == 5003 )
            {
                assert( m_candidates.back().m_endpoint.port() != m_endpointCatalogue.m_myPort );
                ___LOG( "5003 Request(3): " << m_targetPeerKey << " to: " << m_candidates.back().m_endpoint.port() )
            }
            session->sendGetPeerIpRequest( request, m_candidates.back().m_endpoint );
            m_triedPeers.insert( m_candidates.back().m_publicKey );
            m_candidates.pop_back();

            m_timer = session->startTimer( PEER_ANSWER_LIMIT_MS, [this]{ onTimer(); } );
        }
    }
    
    void onTimer()
    {
        if ( m_endpointCatalogue.m_myPort == 5003 )
        {
            ___LOG( "5003 onTimer: " << m_targetPeerKey )
        }

        if ( m_candidates.empty() )
        {
            if ( m_attemptCounter++ < MAX_ATTEMPT_NUMBER )
            {
                m_triedPeers.clear();
                
                for( const auto& bucket : m_endpointCatalogue.m_hashTable.buckets() )
                {
                    for( const auto& peerInfo : bucket.nodes() )
                    {
                        assert( peerInfo.endpoint().port() != m_endpointCatalogue.m_myPort );

                        m_candidates.emplace_back( Candidate{   peerInfo.endpoint(),
                            peerInfo.m_publicKey,
                            xorValue(peerInfo.m_publicKey, m_myPeerKey) } );
                    }
                    for( const auto& bootstrapNode : m_endpointCatalogue.m_bootstraps )
                    {
                        if ( auto it = std::find_if( m_candidates.begin(), m_candidates.end(), [&](const auto& item) {
                            return item.m_publicKey == bootstrapNode.m_publicKey; }); it == m_candidates.end() )
                        {
                            assert( bootstrapNode.m_endpoint.port() != m_endpointCatalogue.m_myPort );
                            m_candidates.emplace_back( Candidate{   bootstrapNode.m_endpoint,
                                bootstrapNode.m_publicKey,
                                xorValue(bootstrapNode.m_publicKey, m_myPeerKey) } );
                        }
                    }
                }
                _SIRIUS_ASSERT( ! m_candidates.empty() )

                // Sort candidates
                std::sort( m_candidates.begin(), m_candidates.end() );
            }
            else
            {
                __LOG( "onTimer: no candidates" )
                if ( auto it = m_endpointCatalogue.m_searcherMap.find(m_targetPeerKey); it != m_endpointCatalogue.m_searcherMap.end() )
                {
                    m_endpointCatalogue.m_searcherMap.erase(it);
                }
                else
                {
                    __LOG_WARN("Corrupted m_searcherMap!");
                }
                return;
            }
        }
        
        sendNextRequest();
    }

    // Response from another peer
    bool onGetPeerIpResponse( const PeerIpResponse& response )
    {
        if ( m_endpointCatalogue.m_myPort == 5003 )
        {
            ___LOG( "on_response: " << m_endpointCatalogue.m_myPort << " " <<response.m_response.size() << " " << response.m_peerKey )
            if ( response.m_response.size() == 1 )
            {
                ___LOG( "on_response: " << response.m_response[0].m_publicKey )
            }
        }

        if ( response.m_response.size() == 1 && response.m_response[0].m_publicKey == m_targetPeerKey )
        {
            const PeerInfo& peerInfo = response.m_response[0];
            
            if ( ! peerInfo.Verify() )
            {
                __LOG_WARN( "PeerSearchInfo::onGetPeerIpResponse: bad sign: " << peerInfo.m_publicKey )
                sendNextRequest();
                return false;
            }
            
            if ( isPeerInfoExpired(peerInfo.m_creationTimeInSeconds) )
            {
                __LOG_WARN( "PeerSearchInfo::onGetPeerIpResponse: expired: " << peerInfo.m_publicKey )
                sendNextRequest();
                return false;
            }
            
            m_endpointCatalogue.m_localEndpointMap[ peerInfo.m_publicKey ] = peerInfo.endpoint();

            if ( int bucketIndex = m_endpointCatalogue.m_hashTable.addPeerInfoOrUpdate( response.m_response[0] ); bucketIndex >= 0 )
            {
                ___LOG( "added: to: " << m_endpointCatalogue.m_myPort << "  key: " << peerInfo.m_publicKey );
                return true;
            }
            
            return false;
        }
        
        for( const auto& peerInfo: response.m_response )
        {
            if ( peerInfo.m_publicKey == m_endpointCatalogue.m_keyPair.publicKey() )
            {
                continue;
            }
            
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
                // skip already requested candidates
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

inline std::unique_ptr<PeerSearchInfo> createPeerSearchInfo( const PeerKey&         myPeerKey,
                                                    const PeerKey&                  targetPeerKey,
                                                    size_t                          bucketIndex,
                                                    OptionalEndpoint                directEndpoint,
                                                    EndpointCatalogueImpl&          endpointCatalogue,
                                                    std::weak_ptr<Session>          session )
{
    return std::make_unique<PeerSearchInfo>( myPeerKey,
                          targetPeerKey,
                          bucketIndex,
                          directEndpoint,
                          endpointCatalogue,
                          session );
}

inline bool onGetPeerIpResponseRedirect( PeerSearchInfo& searchInfo, PeerIpResponse& response )
{
    return searchInfo.onGetPeerIpResponse( response );
}

} // namespace kademlia

std::shared_ptr<kademlia::EndpointCatalogue> createEndpointCatalogue(
                                                             std::weak_ptr<Session>             kademliaTransport,
                                                             const crypto::KeyPair&             keyPair,
                                                             const std::vector<ReplicatorInfo>& bootstraps,
                                                             uint16_t                           myPort,
                                                             bool                               isClient )
{
    return std::make_shared<kademlia::EndpointCatalogueImpl>( kademliaTransport,
                                                              keyPair,
                                                              bootstraps,
                                                              myPort,
                                                              isClient );
}



}}



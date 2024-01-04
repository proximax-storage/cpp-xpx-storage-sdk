//TODO
// - Filter by m_registeredReplicators (with queue of unknowns/leading ones)
// - client skip requests and ???
// - simultaneous requesting
// - update timer -> sendGetPeerIpRequest(of myIp xor 1)
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

namespace sirius { namespace drive { namespace kademlia {

using NodeInfo = ReplicatorInfo;
using namespace ::sirius::drive;

const int PEER_ANSWER_LIMIT_MS = 1000;
const int MAX_ATTEMPT_NUMBER = 300;

class EndpointCatalogueImpl;
class PeerSearchInfo;

void  addCandidatesToSearcher( PeerSearchInfo& searchInfo, PeerIpResponse& response );

inline std::unique_ptr<PeerSearchInfo> createPeerSearchInfo(   const TargetKey&                targetPeerKey,
                                                               size_t                          bucketIndex,
                                                               EndpointCatalogueImpl&          endpointCatalogue,
                                                               std::weak_ptr<Transport>        session,
                                                               bool                            enterToSwarm = false );

class EndpointCatalogueImpl : public EndpointCatalogue
{
public:
    
    using SearcherMap = std::map<TargetKey,std::unique_ptr<PeerSearchInfo>>;

    std::weak_ptr<kademlia::Transport> m_kademliaTransport;
    
    const crypto::KeyPair&          m_keyPair;
    std::vector<NodeInfo>           m_bootstraps;
    uint16_t                        m_myPort;
    bool                            m_isBootstrap = false;
    bool                            m_isClient;

    std::optional<PeerInfo>         m_myPeerInfo;
    Timer                           m_myPeerInfoTimer;
    Timer                           m_updateKademliaTimer;

    std::map<PeerKey,OptionalEndpoint> m_localEndpointMap;

    KademliaHashTable               m_hashTable;
    SearcherMap                     m_searcherMap;
    
    std::set<Key>                   m_registeredReplicators;
    
    std::optional<::sirius::drive::EndpointHandler> m_endpointHandler;

private:
    OptionalEndpoint                m_myEndpoint;

public:

    EndpointCatalogueImpl(  std::weak_ptr<kademlia::Transport>  kademliaTransport,
                            const crypto::KeyPair&              keyPair,
                            const std::vector<NodeInfo>&        bootstraps,
                            uint16_t                            myPort,
                            bool                                isClient )
        :   m_kademliaTransport(kademliaTransport),
            m_keyPair(keyPair),
            m_bootstraps(bootstraps),
            m_myPort(myPort),
            m_isClient(isClient),
            m_hashTable( keyPair.publicKey() )
    {
        if ( ! m_isClient )
        {
            auto it = std::find_if( m_bootstraps.begin(), m_bootstraps.end(), [this]( const auto& item )
                                   {
                return m_keyPair.publicKey() == item.m_publicKey;
            });
            
            if ( it != m_bootstraps.end() )
            {
                m_myPeerInfo = PeerInfo{ m_keyPair.publicKey(), it->m_endpoint };
                m_myPeerInfo->Sign( m_keyPair );
                m_myEndpoint = it->m_endpoint;
                m_bootstraps.erase( it );
                m_isBootstrap = true;
                if ( auto session = m_kademliaTransport.lock(); session )
                {
                    boost::asio::post( session->getContext(), [this]() mutable
                    {
                        enterToSwarm();
                    });
                }
            }
        }
        
        _SIRIUS_ASSERT( m_bootstraps.size() > 0 );

        for( const auto& nodeInfo : m_bootstraps )
        {
            //___LOG( "bootstrap: " << m_myPort << " " << nodeInfo.m_endpoint << " "  << nodeInfo.m_publicKey )
            m_localEndpointMap[nodeInfo.m_publicKey] = nodeInfo.m_endpoint;
        }
        
        // make some delay (for starting dht)
        if ( auto session = m_kademliaTransport.lock(); session )
        {
            boost::asio::post( session->getContext(), [this]() mutable
            {
                start();
            });
        }
    }
    
    ~EndpointCatalogueImpl()
    {
        stopTimers();
    }

    virtual void stopTimers() override
    {
        m_myPeerInfoTimer.cancel();
        m_updateKademliaTimer.cancel();
    }

    virtual PeerKey publicKey() override { return m_keyPair.publicKey(); }

    void start()
    {
        if ( m_isClient )
        {
            enterToSwarm();
            return;
        }
        
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
            ___LOG( m_myPort << " : start: m_myPeerInfoTimer" )
            m_myPeerInfoTimer = session->startTimer( PEER_ANSWER_LIMIT_MS, [this]{ onMyPeerInfoTimer(); } );
        }
    }

    void updateKademlia()
    {
        if ( auto session = m_kademliaTransport.lock(); session )
        {
            m_myPeerInfoTimer = session->startTimer( PEER_UPDATE_SEC, [this]
            {
                for( auto& bucket : m_hashTable.buckets() )
                {
                    bucket.removeExpiredNodes();
                    
                    for( const auto& peerInfo : bucket.nodes() )
                    {
                        if ( shouldPeerInfoBeUpdated( peerInfo.m_creationTimeInSeconds ) )
                        {
                            sendDirectRequest( peerInfo.m_publicKey, peerInfo.endpoint() );
                        }
                    }
                }
                updateKademlia();
            });
        }
    }

    void enterToSwarm()
    {
        // Query peerInfo of bootstraps
        for( auto bootstrapNodeInfo: m_bootstraps )
        {
            size_t bucketIndex = m_hashTable.calcBucketIndex( bootstrapNodeInfo.m_publicKey );
            startSearchPeerInfo( TargetKey{bootstrapNodeInfo.m_publicKey}, bucketIndex );
        }

        PeerKey searchedKey = m_keyPair.publicKey();
        //TODO? maybe searchedKey[0] = searchedKey[0] ^ 0x01;
        searchedKey[31] = searchedKey[31] ^ 0x01;
        
        size_t bucketIndex = m_hashTable.calcBucketIndex( searchedKey );
        startSearchPeerInfo( TargetKey{searchedKey}, bucketIndex, true );
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

    void addClientToLocalEndpointMap( const Key& key ) override
    {
        m_localEndpointMap[key] = {};
    }

    // On some (signed) endpoint discovered
    virtual void onEndpointDiscovered( const Key& key, const OptionalEndpoint& endpoint ) override
    {
        if ( auto it = m_localEndpointMap.find(key); it != m_localEndpointMap.end() )
        {
            it->second = endpoint;
        }
        if ( m_endpointHandler )
        {
            (*m_endpointHandler)( key, endpoint );
        }
    }
    
    virtual void setEndpointHandler( ::sirius::drive::EndpointHandler endpointHandler ) override
    {
        m_endpointHandler = endpointHandler;
    }

    OptionalEndpoint getEndpoint( const PeerKey& key ) override
    {
        // find in local map (usually replicators of common drives)
        //
        if ( auto it = m_localEndpointMap.find(key); it != m_localEndpointMap.end() && it->second )
        {
            __LOG( "getEndpoint (m_localEndpointMap): " << key << " " << it->second.value() )
            return it->second;
        }
    
        // find in Kademlia hash table
        //
        size_t bucketIndex;
        if ( const auto* peerInfo = m_hashTable.getPeerInfo( key, bucketIndex ); peerInfo != nullptr )
        {
            __LOG( "getEndpoint (m_hashTable): " << key << " " << peerInfo->endpoint() )
            return peerInfo->endpoint();
        }
        else if ( m_hashTable.couldBeAdded( key ) )
        {
            __LOG( "getEndpoint : startSearchPeerInfo" << key )
            // search unknown peer
            size_t bucketIndex = m_hashTable.calcBucketIndex( key );
            startSearchPeerInfo( TargetKey{key}, bucketIndex );
        }

        __LOG( "getEndpoint {no-endpoint}: " << key )
        return {};
    }
    
    void startSearchPeerInfo( const TargetKey& key, size_t bucketIndex, bool enterToSwarm = false )
    {
        if ( auto it = m_searcherMap.find( key ); it == m_searcherMap.end() )
        {
            m_searcherMap[key] = createPeerSearchInfo( key,
                                                      bucketIndex,
                                                      *this,
                                                      m_kademliaTransport,
                                                      enterToSwarm );
        }
    }

    const PeerInfo* getPeerInfo( const PeerKey& key ) override
    {
        if ( key == m_keyPair.publicKey() )
        {
            if ( m_myPeerInfo )
            {
                return & m_myPeerInfo.value();
            }
            return nullptr;
        }
        
        // find in Kademlia hash table
        //
        size_t bucketIndex;
        if ( const auto* peerInfo = m_hashTable.getPeerInfo( key, bucketIndex ); peerInfo != nullptr )
        {
            return peerInfo;
        }
        
        // so far not found
        return nullptr;
    }
    
    std::string onGetMyIpRequest( const std::string& request, boost::asio::ip::udp::endpoint requesterEndpoint ) override
    {
        ___LOG( "onGetMyIpRequest: from: " << requesterEndpoint << " to: " << m_myPort )
        
        if ( m_isClient )
        {
            __LOG_WARN( "client should not receive GetMyIpRequest")
            return "";
        }
        
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
        ___LOG( "onGetMyIpResponse: " )
        if ( m_isClient )
        {
            __LOG_WARN( "client should not receive MyIpResponse")
            return;
        }
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
            
            // in local subnetworks global address (and port) could be changed to logical
            if ( m_myPort != response.m_response.m_port )
            {
                __LOG_WARN( "ignore bad MyIpResponse" )
                return;
            }

            ___LOG( "onGetMyIpResponse: " << m_myPort << " " << response.m_response.endpoint() )
            bool firstResponse = !m_myPeerInfo.has_value();
            m_myPeerInfo = PeerInfo{ m_keyPair.publicKey(), response.m_response.endpoint() };
            m_myPeerInfo->Sign( m_keyPair );
            m_myPeerInfoTimer.cancel();
            
            // check response faults
            //
            if ( m_myEndpoint &&
                (response.m_response.endpoint().port() != m_myEndpoint->port() || response.m_response.endpoint().address() != m_myEndpoint->address()) )
            {
                if ( m_isBootstrap )
                {
                    __LOG_WARN( "Invalid replicators.json! wrong my port number! (invalid bootstrap list): " << response.m_response.endpoint() << " vs " << *m_myEndpoint )
                }
                else
                {
                    __LOG_WARN( "Unexpected error in related to 'MyIpResponse': " << response.m_response.endpoint() << " vs " << *m_myEndpoint )
                }
            }
            
            // start Kademlia
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
        ___LOG( "onGetPeerIpRequest: " << m_myPort << " from: " << requesterEndpoint.port() )

        if ( m_isClient )
        {
            __LOG_WARN( "client should not receive PeerIpRequest")
            return "";
        }

        try
        {
            std::istringstream is( requestStr, std::ios::binary );
            cereal::PortableBinaryInputArchive archive(is);
            PeerIpRequest request;
            archive( request );
            
            ___LOG( "onGetPeerIpRequest: " << m_myPort << " from: " << requesterEndpoint.port() << " of: " << request.m_targetKey )

            // Query requester peerInfo if it could be added to my hashtable
            if ( !request.m_requesterIsClient && request.m_requesterKey.m_key != m_keyPair.publicKey() )
            {
                if ( m_localEndpointMap.find(request.m_requesterKey.m_key) == m_localEndpointMap.end() )
                {
                    if ( m_hashTable.couldBeAdded( request.m_requesterKey.m_key ) )
                    {
                        size_t bucketIndex;
                        const auto* peerInfo = m_hashTable.getPeerInfo( request.m_requesterKey.m_key, bucketIndex );
                        if ( peerInfo == nullptr || isPeerInfoExpired( peerInfo->m_creationTimeInSeconds) )
                        {
                            ___LOG( "sendDirectRequest: " << " (from: " << requesterEndpoint.port() << ") " << m_myPort << " of: " << request.m_requesterKey )
                            sendDirectRequest( request.m_requesterKey.m_key, requesterEndpoint );
                        }
                    }
                }
            }
            
            std::vector<PeerInfo> peers;
            
            // Is target peer my peer?
            //
            if ( request.m_targetKey.m_key == m_keyPair.publicKey() )
            {
                if ( ! m_myPeerInfo )
                {
                    ___LOG( "!m_myPeerInfo: " << m_myPort )
                    return "";
                }
                else
                {
                    m_myPeerInfo->updateCreationTime( m_keyPair );
                    //___LOG( "peers.push_back: " << m_myPort << " to: " << requesterEndpoint.port() << " " << m_myPeerInfo->m_publicKey )
                    peers.push_back(*m_myPeerInfo);
                }
            }
            else
            {
                // find in Kademlia hash table
                peers  = m_hashTable.findClosestNodes( request.m_targetKey.m_key );
            }
            
            // return response
            //
            PeerIpResponse response{ request.m_targetKey, std::move(peers) };

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

    void sendDirectRequest( const PeerKey& targetKey, boost::asio::ip::udp::endpoint endpoint )
    {
        ___LOG( "sendDirectRequest: to: " << endpoint.port() << " myPort: " << m_myPort  << " of: " << targetKey )
        if ( auto session = m_kademliaTransport.lock(); session )
        {
            PeerIpRequest request2{ m_isClient, TargetKey{targetKey}, RequesterKey{m_keyPair.publicKey()} };
            session->sendGetPeerIpRequest( request2, endpoint );
        }
    }
    
    void onGetPeerIpResponse( const std::string& responseStr, boost::asio::ip::udp::endpoint responserEndpoint ) override
    {
        try
        {
            // Unpack response
            std::istringstream is( responseStr, std::ios::binary );
            cereal::PortableBinaryInputArchive archive(is);
            PeerIpResponse response;
            archive( response );
            
            ___LOG( "onGetPeerIpResponse: from: " << responserEndpoint.port() << " myPort: " << m_myPort << " of: " << response.m_targetKey << " " << response.m_response.size() )

            //
            // At first we add all peerInfo from response to our DHT
            //
            for( const auto& peerInfo : response.m_response )
            {
                if ( ! peerInfo.Verify() )
                {
                    __LOG_WARN( "! peerInfo.Verify()" )
                    continue;
                }

                if ( isPeerInfoExpired(peerInfo.m_creationTimeInSeconds) )
                {
                    __LOG_WARN( "PeerSearchInfo::onGetPeerIpResponse: expired: " << peerInfo.m_publicKey )
                    continue;
                }
                
                bool peerInfoIsNew = m_localEndpointMap.find(peerInfo.m_publicKey) == m_localEndpointMap.end();
                
                // add to local map
                m_localEndpointMap[peerInfo.m_publicKey] = peerInfo.endpoint();
                ___LOG( " added to local map: " << m_myPort << " of: " << peerInfo.m_publicKey << " " << (m_localEndpointMap.find(peerInfo.m_publicKey)!=m_localEndpointMap.end() ))

                // try add to kademlia
                if ( int bucketIndex = m_hashTable.addPeerInfoOrUpdate( peerInfo ); bucketIndex >= 0 )
                {
                    ___LOG( " (direct?) added: " << m_myPort << " of: " << peerInfo.m_publicKey )
                }

                if ( auto it = m_searcherMap.find( TargetKey{peerInfo.m_publicKey} ); it != m_searcherMap.end()  )
                {
                    ___LOG( " added: " << m_myPort << " of: " << peerInfo.m_publicKey << " " << (m_localEndpointMap.find(peerInfo.m_publicKey)!=m_localEndpointMap.end() ))
                    m_searcherMap.erase(it);

                    if ( response.m_targetKey.m_key == peerInfo.m_publicKey && peerInfoIsNew )
                    {
                        // Let 'target' will know our peerInfo
                        // (after 'target' received this request it will send request to me)
                        // (responses for direct request are skipped, because 'it != m_searcherMap.end()' )
                        if ( auto session = m_kademliaTransport.lock(); session && m_myPeerInfo )
                        {
                            m_myPeerInfo->updateCreationTime( m_keyPair );
                            PeerIpResponse info{ TargetKey{m_keyPair.publicKey()}, {*m_myPeerInfo} };
                            session->sendDirectPeerInfo( info, peerInfo.endpoint() );
                        }
                    }
                }
            }
            
            // add candidates
            //
            if ( ! response.m_response.empty() )
            {
                if ( response.m_response.size() != 1 || response.m_response[0].m_publicKey != response.m_targetKey.m_key )
                {
                    if ( auto it = m_searcherMap.find(response.m_targetKey); it != m_searcherMap.end() )
                    {
                        addCandidatesToSearcher( *it->second, response );
                    }
                }
            }
        }
        catch(...) {
            __LOG_WARN( "exception in onGetPeerIpResponse" )
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
            boost::asio::post( kademliaTransport->getContext(), [=, this]() mutable
            {
                for ( auto& [key,searchInfo] : m_searcherMap )
                {
                    ___LOG( "m_searcherMap " << m_myPort << ": " << m_searcherMap.size() << ": " << key );
                }
                
                if ( ! m_myPeerInfo.has_value() )
                {
                    ___LOG( "! m_myPeerInfo " << m_myPort )
                }
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
            return m_xorValue>item.m_xorValue;
        }
    };
    
    const TargetKey         m_targetPeerKey;
    const PeerKey           m_myPeerKey;
    
    EndpointCatalogueImpl&   m_endpointCatalogue;
    std::weak_ptr<Transport> m_session;
    bool                     m_enterToSwarm;

    std::vector<Candidate>  m_candidates;
    std::set<PeerKey>       m_triedPeers;
    Timer                   m_timer;
    int                     m_attemptCounter = 0;

public:

    //PeerSearchInfo( const PeerSearchInfo& ) = default;

    PeerSearchInfo( const TargetKey&                targetPeerKey,
                    int                             bucketIndex,
                    EndpointCatalogueImpl&          endpointCatalogue,
                    std::weak_ptr<Transport>        session,
                    bool                            enterToSwarm )
    :
        m_targetPeerKey(targetPeerKey),
        m_myPeerKey(endpointCatalogue.m_keyPair.publicKey()),
        m_endpointCatalogue(endpointCatalogue),
        m_session(session),
        m_enterToSwarm(enterToSwarm)
    {
        ___LOG("search start: " << m_endpointCatalogue.m_myPort << " of: " << m_targetPeerKey )

        for( const auto& peerInfo : m_endpointCatalogue.m_hashTable.buckets()[bucketIndex].nodes() )
        {
            m_candidates.emplace_back( Candidate{   peerInfo.endpoint(),
                                                    peerInfo.m_publicKey,
                                                    xorValue(peerInfo.m_publicKey, m_targetPeerKey.m_key) } );
        }

        while ( m_candidates.empty() )
        {
            bucketIndex--;

            if ( bucketIndex<0 )
            {
                for( const auto& bootstrapNode : m_endpointCatalogue.m_bootstraps )
                {
                    m_candidates.emplace_back( Candidate{   bootstrapNode.m_endpoint,
                                                            bootstrapNode.m_publicKey,
                                                            xorValue(bootstrapNode.m_publicKey, m_targetPeerKey.m_key) } );
                }
                break;
            }

            for( const auto& peerInfo : m_endpointCatalogue.m_hashTable.buckets()[bucketIndex].nodes() )
            {
                m_candidates.emplace_back( Candidate{   peerInfo.endpoint(),
                                                        peerInfo.m_publicKey,
                                                        xorValue(peerInfo.m_publicKey, m_targetPeerKey.m_key) } );
            }
        }
        
        // Sort candidates
        std::sort( m_candidates.begin(), m_candidates.end() );
        
        sendNextRequest();
    }

    ~PeerSearchInfo()
    {
        ___LOG( "~PeerSearchInfo: " << m_endpointCatalogue.m_myPort << " of: " << m_targetPeerKey )
        m_timer.cancel();
    }

    inline void sendNextRequest()
    {
        if ( auto session = m_session.lock(); session )
        {
            PeerIpRequest request{ m_endpointCatalogue.m_isClient, m_targetPeerKey, RequesterKey{m_myPeerKey} };
         
            if ( ! m_candidates.empty() )
            {
                ___LOG( "sendGetPeerIpRequest: to: " << m_candidates.back().m_endpoint.port() << " myPort: " << m_endpointCatalogue.m_myPort << " of: " << m_targetPeerKey )
                session->sendGetPeerIpRequest( request, m_candidates.back().m_endpoint );
                m_triedPeers.insert( m_candidates.back().m_publicKey );
                m_candidates.pop_back();
            }
            
            m_timer = session->startTimer( PEER_ANSWER_LIMIT_MS, [this]{ onTimer(); } );
        }
    }
    
    void onTimer()
    {
        if ( m_candidates.empty() )
        {
            if ( m_enterToSwarm )
            {
                cancelSearch();
                return;
            }

            //___LOG( "m_candidates.empty(): " << m_endpointCatalogue.m_myPort << " of: " << m_targetPeerKey )
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
                            xorValue(peerInfo.m_publicKey, m_targetPeerKey.m_key) } );
                    }
                    for( const auto& bootstrapNode : m_endpointCatalogue.m_bootstraps )
                    {
                        if ( auto it = std::find_if( m_candidates.begin(), m_candidates.end(), [&](const auto& item) {
                            return item.m_publicKey == bootstrapNode.m_publicKey; }); it == m_candidates.end() )
                        {
                            assert( bootstrapNode.m_endpoint.port() != m_endpointCatalogue.m_myPort );
                            m_candidates.emplace_back( Candidate{   bootstrapNode.m_endpoint,
                                bootstrapNode.m_publicKey,
                                xorValue(bootstrapNode.m_publicKey, m_targetPeerKey.m_key) } );
                        }
                    }
                }
                _SIRIUS_ASSERT( ! m_candidates.empty() )

                // Sort candidates
                std::sort( m_candidates.begin(), m_candidates.end() );
            }
            else
            {
                //TODO?
                ___LOG( "onTimer: no candidates: " << m_endpointCatalogue.m_myPort << " of: " << m_targetPeerKey )
                cancelSearch();
                return;
            }
        }
        
        sendNextRequest();
    }
    
    void cancelSearch()
    {
        if ( auto it = m_endpointCatalogue.m_searcherMap.find(m_targetPeerKey); it != m_endpointCatalogue.m_searcherMap.end() )
        {
            m_endpointCatalogue.m_searcherMap.erase(it);
        }
        else
        {
            __LOG_WARN("Corrupted m_searcherMap!");
        }
    }

    // Response from another peer
    void addCandidatesFromResponse( const PeerIpResponse& response )
    {
        _SIRIUS_ASSERT( response.m_response.size() > 0 )

        _SIRIUS_ASSERT( response.m_response.size() != 1 || response.m_response[0].m_publicKey != m_targetPeerKey.m_key )

        for( const auto& peerInfo: response.m_response )
        {
            // skip my key
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
                xorValue( peerInfo.m_publicKey, m_targetPeerKey.m_key ) } );
        }

        std::sort( m_candidates.begin(), m_candidates.end() );
        
        sendNextRequest();
    }
};


inline void addCandidatesToSearcher( PeerSearchInfo& searchInfo, PeerIpResponse& response )
{
    searchInfo.addCandidatesFromResponse( response );
}

inline std::unique_ptr<PeerSearchInfo> createPeerSearchInfo(    const TargetKey&                targetPeerKey,
                                                                size_t                          bucketIndex,
                                                                EndpointCatalogueImpl&          endpointCatalogue,
                                                                std::weak_ptr<Transport>        session,
                                                                bool                            enterToSwarm )
{
    return std::make_unique<PeerSearchInfo>(  targetPeerKey,
                                              (int)bucketIndex,
                                              endpointCatalogue,
                                              session,
                                              enterToSwarm );
}

} // namespace kademlia

std::shared_ptr<kademlia::EndpointCatalogue> createEndpointCatalogue(
                                                             std::weak_ptr<kademlia::Transport>             kademliaTransport,
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



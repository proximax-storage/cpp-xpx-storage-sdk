#include "drive/Session.h"
#include "drive/Kademlia.h"
#include "KademliaBucket.h"
#include "KademliaNode.h"
#include "drive/Timer.h"

namespace sirius { namespace drive { namespace kademlia {

class PeerSeeker
{
    PeerKey                 m_targetPeerKey;
    std::vector<NodeInfo>   m_candidates;
    std::set<PeerKey>       m_loserPeers;
    
    EndpointCatalogue&      m_endpointCatalogue;
    std::weak_ptr<Session>  m_session;
    Timer                   m_timer;
    
    const int PEER_ASWER_LIMIT_MS = 1000;
    
    PeerSeeker( const PeerKey&                  targetPeerKey,
               const std::vector<NodeInfo>&     candidates,
               EndpointCatalogue&               endpointCatalogue,
               std::weak_ptr<Session>           session )
    :
        m_targetPeerKey(targetPeerKey),
        m_candidates(candidates),
        m_endpointCatalogue(endpointCatalogue),
        m_session(session)
    {
        if ( auto session = m_session.lock(); session )
        {
            PeerIpRequest request{ session->isClient(), targetPeerKey, m_endpointCatalogue.publicKey() };
            //TODO? candidates[0].m_endpoint should be replaced by nearest node
            session->sendGetPeerIpRequest( request, candidates[0].m_endpoint );
            m_timer = session->startTimer( PEER_ASWER_LIMIT_MS, []{} );
        }
    }
    
    void onGetMyIpResponse( const std::string& )
    {
    }
};

class EndpointCatalogueImpl : public EndpointCatalogue
{
    using SeekerMap = std::map<PeerKey,PeerSeeker>;
    
    Transport&                      m_kademliaTransport;
    const crypto::KeyPair&          m_keyPair;
    std::vector<NodeInfo>           m_bootstraps;
    uint8_t                         m_myPort;
    bool                            m_isClient;

    std::map<PeerKey,boost::asio::ip::udp::endpoint> m_localEndpointMap;
    
    HashTable                      m_hashTable;
    SeekerMap                      m_seekerMap;

private:
    boost::asio::ip::udp::endpoint  m_myIp;

public:
    
    EndpointCatalogueImpl(  Transport&                    kademliaTransport,
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
        if ( auto it = m_localEndpointMap.find(key); it != m_localEndpointMap.end() )
        {
            return it->second;
        }
        
//        m_hashTable->getEndpoint( key );
        
        return {};
    }

    MyIpResponse    onGetMyIpRequest( const std::string& ) override
    {
        return MyIpResponse{};
    }
    
    void            onGetMyIpResponse( const std::string& ) override
    {
    }
    
    PeerIpResponse  onGetPeerIpRequest( const std::string& ) override
    {
        return PeerIpResponse{};
    }

    void            onGetPeerIpResponse( const std::string& ) override
    {
    }

};


std::unique_ptr<kademlia::EndpointCatalogue> createEndpointCatalogue(
                                                             Transport&                     kademliaTransport,
                                                             const crypto::KeyPair&         keyPair,
                                                             const std::vector<NodeInfo>&   bootstraps,
                                                             uint8_t                        myPort,
                                                             bool                           isClient )
{
    return std::make_unique<kademlia::EndpointCatalogueImpl>( kademliaTransport,
                                                              keyPair,
                                                              bootstraps,
                                                              myPort,
                                                              isClient );
}

}}}



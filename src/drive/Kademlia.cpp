#include "drive/Session.h"
#include "Kademlia.h"
#include "KademliaBucket.h"
#include "KademliaNode.h"

namespace sirius { namespace drive { namespace kademlia {

struct RequestPullItem
{
    
};

class KademliaDht
{
    
    
};


class EndpointCatalogueImpl : public EndpointCatalogue
{
    Transport&                      m_kademliaTransport;
    const crypto::KeyPair&          m_keyPair;
    std::vector<NodeInfo>           m_bootstraps;
    uint8_t                         m_myPort;
    bool                            m_isClient;

    std::map<PeerKey,boost::asio::ip::udp::endpoint> m_localEndpointMap;
    
    KademliaDht                     m_dht;

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

    std::optional<boost::asio::ip::udp::endpoint> getEndpoint( PeerKey& key )
    {
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



/*
*** Copyright 2023 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include <array>
#include <set>
#include <cereal/types/array.hpp>
#include <cereal/types/memory.hpp>
#include <cereal/archives/portable_binary.hpp>
#include <chrono>

namespace sirius { namespace drive { namespace kademlia {

using PeerKey       = std::array<uint8_t,32>;

inline PeerKey operator^( const PeerKey& a, const PeerKey& b )
{
    PeerKey outKey;
    for( size_t i=0; i<outKey.size(); i++ )
    {
        outKey[i] = a[i] ^ b[i];
    }
    return outKey;
}

//-----------------------------------------------------
// PeerInfo
//-----------------------------------------------------
struct PeerInfo
{
    PeerKey     m_publicKey;
    std::string m_address;
    uint16_t    m_port;
    uint64_t    m_timeInSeconds; // uint64_t now = duration_cast(std::chrono::steady_clock::now().time_since_epoch()).count();
    Signature   m_signature;
    
    //todo for debugging
    PeerInfo() = default;

    //PeerInfo( const PeerKey& peerKey ) : m_peerKey(peerKey) {}

    PeerInfo( const PeerKey& peerKey, const boost::asio::ip::udp::endpoint& endpoint )
      : m_publicKey(peerKey),
        m_address(endpoint.address().to_string()),
        m_port(endpoint.port()),
        m_timeInSeconds( std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count() )
    {}
    
    uint64_t secondsFromNow() const { return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count() - m_timeInSeconds; }
    
    template<class Archive>
    void serialize(Archive &arch)
    {
        arch(m_publicKey);
        arch(m_address);
        arch(m_port);
        arch(m_timeInSeconds);
        arch(m_signature);
    }
    
    boost::asio::ip::udp::endpoint endpoint() const {
        return boost::asio::ip::udp::endpoint{ boost::asio::ip::make_address(m_address), uint16_t(m_port) };
    }
    
    bool Verify() const
    {
        return crypto::Verify( m_publicKey,
                              {
            utils::RawBuffer{ (const uint8_t*)&m_publicKey[0], sizeof(m_publicKey) },
            utils::RawBuffer{ (const uint8_t*)m_address.c_str(), m_address.size() },
            utils::RawBuffer{ (const uint8_t*)m_port, sizeof(m_port) },
            utils::RawBuffer{ (const uint8_t*)m_timeInSeconds, sizeof(m_timeInSeconds) },
        },
        m_signature );
    }
    
    void Sign( const crypto::KeyPair& keyPair )
    {
        crypto::Sign( keyPair,
                     {
            utils::RawBuffer{ (const uint8_t*)&m_publicKey[0], sizeof(m_publicKey) },
            utils::RawBuffer{ (const uint8_t*)m_address.c_str(), m_address.size() },
            utils::RawBuffer{ (const uint8_t*)m_port, sizeof(m_port) },
            utils::RawBuffer{ (const uint8_t*)m_timeInSeconds, sizeof(m_timeInSeconds) },
        },
        m_signature );
    }
    
    bool operator<(const PeerInfo& item) const { return m_publicKey < item.m_publicKey; }
};

inline bool operator==(const PeerInfo& a,const PeerInfo& b)  { return a.m_publicKey == b.m_publicKey; }

//-----------------------------------------------------
// Request 'get-my-ip'
//-----------------------------------------------------
struct MyIpRequest
{
    // m_myPort is using to skip local addresses
    // (Client won't possibly do this request)
    // If requester is client m_myPort MUST BE 0 !!!
    // (Because client could not be in the same local network as bootstrap node)
    uint16_t  m_myPort = 0;
    
    template<class Archive>
    void serialize(Archive &arch)
    {
        arch(m_myPort);
    }
};

//-----------------------------------------------------
// Response from queried peer about 'get-my-ip'
//-----------------------------------------------------
struct MyIpResponse
{
    bool     m_badPort = true;  // may be requester is in local network
    
    // This info will contains my ip (not sender), but key and signature of sender
    PeerInfo m_response;
    
    template<class Archive>
    void serialize(Archive &arch)
    {
        arch(m_badPort);
        arch(m_response);
    }
    
    //todo for debugging
    MyIpResponse() = default;
    
    MyIpResponse( const crypto::KeyPair& keyPair, const boost::asio::ip::udp::endpoint& queriedEndpoint )
    : m_badPort(false), m_response( keyPair.publicKey().array(), queriedEndpoint )
    {
        m_response.m_timeInSeconds = std::chrono::steady_clock::now().time_since_epoch().count();
        m_response.Sign( keyPair );
    }
    
    bool verify() const { return m_response.Verify(); }
    
    //void sign( const crypto::KeyPair& keyPair ) { m_response.Sign(keyPair); }
};

//-----------------------------------------------------
// Kademlia-Request to another peer 'get-ip'
//-----------------------------------------------------
struct PeerIpRequest
{
    bool m_requesterIsClient = false;
    PeerKey m_peerKey;
    PeerKey m_requesterKey;

    template<class Archive>
    void serialize(Archive &arch)
    {
        arch(m_peerKey);
        arch(m_requesterKey);
    }
};

//-----------------------------------------------------
// Response on 'get-ip' request
//-----------------------------------------------------
struct PeerIpResponse
{
    // if found then 'm_response' has single PeerInfo where m_response.m_peerKey == m_peerKey
//    bool                    m_found;
    
    PeerKey                 m_peerKey;
    std::vector<PeerInfo>   m_response;
    
    template<class Archive>
    void serialize(Archive &arch)
    {
        //arch(m_found);
        arch(m_peerKey);
        arch(m_response);
    }
};

//-----------------------------------------------------
// Kademlia Transport interface
//-----------------------------------------------------
class Transport
{
public:
    virtual ~Transport() = default;

    //
    // Requests
    //
    virtual void sendGetMyIpRequest( const MyIpRequest& request, boost::asio::ip::udp::endpoint endpoint ) = 0;
    virtual void sendGetPeerIpRequest( const PeerIpRequest& request, boost::asio::ip::udp::endpoint endpoint ) = 0;

    virtual void onGetMyIpRequest( const std::string& ) = 0;
    virtual void onGetPeerIpRequest( const std::string& ) = 0;

    virtual void onGetMyIpResponse( const std::string& ) = 0;
    virtual void onGetPeerIpResponse( const std::string& ) = 0;
};

//-----------------------------------------------------
// EndpointCatalogue interface
//-----------------------------------------------------
class EndpointCatalogue
{
public:
    virtual ~EndpointCatalogue() = default;
    
    virtual PeerKey publicKey() = 0;

    virtual std::optional<boost::asio::ip::udp::endpoint> getEndpoint( PeerKey& key ) =0;

    // 'get-my-ip'
    virtual std::string     onGetMyIpRequest( const std::string& request, boost::asio::ip::udp::endpoint requesterEndpoint ) = 0;
    virtual void            onGetMyIpResponse( const std::string& ) = 0;

    // 'get-ip'
    virtual std::string     onGetPeerIpRequest( const std::string& ) = 0;
    virtual void            onGetPeerIpResponse( const std::string& ) = 0;
};


} // namespace kademlia

class Session;
std::unique_ptr<kademlia::EndpointCatalogue> createEndpointCatalogue(
                                                    std::weak_ptr<Session>              kademliaTransport,
                                                    const crypto::KeyPair&              keyPair,
                                                    const std::vector<ReplicatorInfo>&  bootstraps,
                                                    uint8_t                             myPort,
                                                    bool                                isClient );

}}



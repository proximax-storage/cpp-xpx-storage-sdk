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

//-----------------------------------------------------
// PeerInfo
//-----------------------------------------------------
struct PeerInfo
{
    PeerKey     m_peerKey;
    std::string m_address;
    uint16_t    m_port;
    uint64_t    m_time; // uint64_t now = duration_cast(std::chrono::steady_clock::now().time_since_epoch()).count();
    Signature   m_signature;
    
    //todo for debugging
    PeerInfo() = default;

    //PeerInfo( const PeerKey& peerKey ) : m_peerKey(peerKey) {}

    PeerInfo( const PeerKey& peerKey, boost::asio::ip::udp::endpoint& endpoint )
      : m_peerKey(peerKey),
        m_address(endpoint.address().to_string()),
        m_port(endpoint.port()),
        m_time( std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count() )
    {}
    
    uint64_t secondsFromNow() const { return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count() - m_time; }
    
    template<class Archive>
    void serialize(Archive &arch)
    {
        arch(m_peerKey);
        arch(m_address);
        arch(m_port);
        arch(m_time);
        arch(m_signature);
    }
    
    bool Verify() const
    {
        return crypto::Verify( m_peerKey,
                              {
            utils::RawBuffer{ (const uint8_t*)&m_peerKey[0], sizeof(m_peerKey) },
            utils::RawBuffer{ (const uint8_t*)m_address.c_str(), m_address.size() },
            utils::RawBuffer{ (const uint8_t*)m_port, sizeof(m_port) },
            utils::RawBuffer{ (const uint8_t*)m_time, sizeof(m_time) },
        },
        m_signature );
    }
    
    void Sign( const crypto::KeyPair& keyPair )
    {
        crypto::Sign( keyPair,
                     {
            utils::RawBuffer{ (const uint8_t*)&m_peerKey[0], sizeof(m_peerKey) },
            utils::RawBuffer{ (const uint8_t*)m_address.c_str(), m_address.size() },
            utils::RawBuffer{ (const uint8_t*)m_port, sizeof(m_port) },
            utils::RawBuffer{ (const uint8_t*)m_time, sizeof(m_time) },
        },
        m_signature );
    }
};

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
    
    MyIpResponse( const crypto::KeyPair& keyPair, boost::asio::ip::udp::endpoint& queriedEndpoint )
    : m_response( keyPair.publicKey().array(), queriedEndpoint )
    {
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
// Kademlia interface
//-----------------------------------------------------
class Kademlia
{
public:
    virtual ~Kademlia() = default;
    
    // on 'get-my-ip' request
    virtual MyIpResponse    onGetMyIpRequest( const std::string& ) = 0;
    virtual void            onGetMyIpResponse( const std::string& ) = 0;

    // on 'get-ip' request
    virtual PeerIpResponse  onGetPeerIpRequest( const std::string& ) = 0;
    virtual void            onGetPeerIpResponse( const std::string& ) = 0;
};

//-----------------------------------------------------
// KademliaTransport interface
//-----------------------------------------------------
class KademliaTransport
{
public:
    virtual ~KademliaTransport() = default;

    //
    // Requests
    //
    virtual void sendGetMyIpRequest( const MyIpRequest& request ) = 0;
    virtual void sendGetPeerIpRequest( const MyIpRequest& request ) = 0;

    virtual void onGetMyIpRequest( const std::string& ) = 0;
    virtual void onGetPeerIpRequest( const std::string& ) = 0;

    //
    // Responses
    //
    virtual void sendGetMyIpResponse( const MyIpResponse& request ) = 0;
    virtual void sendGetPeerIpResponse( const MyIpRequest& request ) = 0;

    virtual void onGetMyIpResponse( const std::string& ) = 0;
    virtual void onGetPeerIpResponse( const std::string& ) = 0;
};

struct BoostrapNodeInfo {
    PeerKey                         m_peerKey;
    boost::asio::ip::udp::endpoint  m_endpoint;
    
    bool operator< (const BoostrapNodeInfo& info) const { return m_peerKey < info.m_peerKey; }
};

}}}



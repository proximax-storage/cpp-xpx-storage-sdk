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

namespace sirius { namespace drive {

using EndpointHandler = std::function<void(const Key&,const OptionalEndpoint&)>;

//TODO?
struct KademliaDbgInfo
{
    std::atomic<size_t> m_requestCounter{0};
    std::atomic<size_t> m_peerCounter{0};
};
using KademliaDbgFunc = std::function<void(const KademliaDbgInfo&)>;


namespace kademlia {

using PeerKey = ::sirius::Key;

struct TargetKey
{
    PeerKey m_key;

    constexpr bool operator<(const TargetKey& rhs) const {
        return m_key < rhs.m_key;
    }

    friend std::ostream& operator<< (std::ostream& stream, const TargetKey& key) { stream << key.m_key; return stream; }
};

struct RequesterKey
{
    PeerKey m_key;
    
    constexpr bool operator<(const RequesterKey& rhs) const {
        return m_key < rhs.m_key;
    }

    friend std::ostream& operator<< (std::ostream& stream, const RequesterKey& key) { stream << key.m_key; return stream; }
};


inline PeerKey xorValue( const PeerKey& a, const PeerKey& b )
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
    uint64_t    m_creationTimeInSeconds; // currentTimeSeconds()
    Signature   m_signature;
    
    PeerInfo() = default;

    PeerInfo( const PeerKey& peerKey, const boost::asio::ip::udp::endpoint& endpoint )
      : m_publicKey(peerKey),
        m_address(endpoint.address().to_string()),
        m_port(endpoint.port()),
        m_creationTimeInSeconds( currentTimeSeconds() )
    {}
    
    uint64_t secondsFromNow() const { return currentTimeSeconds() - m_creationTimeInSeconds; }
    
    template<class Archive>
    void serialize(Archive &arch)
    {
        arch(m_publicKey);
        arch(m_address);
        arch(m_port);
        arch(m_creationTimeInSeconds);
        arch(m_signature);
    }
    
    boost::asio::ip::udp::endpoint endpoint() const {
        return boost::asio::ip::udp::endpoint{ boost::asio::ip::make_address(m_address), uint16_t(m_port) };
    }
    
    void updateCreationTime( const crypto::KeyPair& keyPair )
    {
        m_creationTimeInSeconds = currentTimeSeconds();
        Sign( keyPair );
    }
    
    bool Verify() const
    {
        return crypto::Verify( m_publicKey,
                              {
            utils::RawBuffer{ (const uint8_t*)&m_publicKey[0], sizeof(m_publicKey) },
            utils::RawBuffer{ (const uint8_t*)m_address.c_str(), m_address.size() },
            utils::RawBuffer{ (const uint8_t*)&m_port, sizeof(m_port) },
            utils::RawBuffer{ (const uint8_t*)&m_creationTimeInSeconds, sizeof(m_creationTimeInSeconds) },
        },
        m_signature );
    }
    
    void Sign( const crypto::KeyPair& keyPair )
    {
        crypto::Sign( keyPair,
                     {
            utils::RawBuffer{ (const uint8_t*)&m_publicKey[0], sizeof(m_publicKey) },
            utils::RawBuffer{ (const uint8_t*)m_address.c_str(), m_address.size() },
            utils::RawBuffer{ (const uint8_t*)&m_port, sizeof(m_port) },
            utils::RawBuffer{ (const uint8_t*)&m_creationTimeInSeconds, sizeof(m_creationTimeInSeconds) },
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
    // m_myPort is used to skip local addresses
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
        m_response.m_creationTimeInSeconds = currentTimeSeconds();
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
    PeerIpRequest() = default;

    explicit PeerIpRequest( bool requesterIsClient, const TargetKey& targetKey, const RequesterKey& requesterKey )
        : m_requesterIsClient(requesterIsClient), m_targetKey(targetKey), m_requesterKey( requesterKey )
    {}

    bool         m_requesterIsClient = false;
    TargetKey    m_targetKey;
    RequesterKey m_requesterKey;

    template<class Archive>
    void serialize(Archive &arch)
    {
        arch(m_requesterIsClient);
        arch(m_targetKey.m_key);
        arch(m_requesterKey.m_key);
    }
};

//-----------------------------------------------------
// Response on 'get-ip' request
//-----------------------------------------------------
struct PeerIpResponse
{
    PeerIpResponse() = default;
    
    explicit PeerIpResponse( const TargetKey& targetKey, std::vector<PeerInfo>&& response )
        : m_targetKey(targetKey), m_response( std::move(response))
    {}
    
    TargetKey               m_targetKey;
    std::vector<PeerInfo>   m_response;
    
    template<class Archive>
    void serialize(Archive &arch)
    {
        //arch(m_found);
        arch(m_targetKey.m_key);
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
    virtual void sendDirectPeerInfo( const PeerIpResponse& response, boost::asio::ip::udp::endpoint endpoint ) = 0;
    
    virtual std::string onGetMyIpRequest( const std::string&, boost::asio::ip::udp::endpoint requesterEndpoint ) = 0;
    virtual std::string onGetPeerIpRequest( const std::string&, boost::asio::ip::udp::endpoint requesterEndpoint ) = 0;
    
    virtual void onGetMyIpResponse( const std::string&, boost::asio::ip::udp::endpoint responserEndpoint ) = 0;
    virtual void onGetPeerIpResponse( const std::string&, boost::asio::ip::udp::endpoint responserEndpoint ) = 0;
    
    virtual boost::asio::io_context& getContext() = 0;
    virtual Timer     startTimer( int milliseconds, std::function<void()> func ) = 0;
};

//-----------------------------------------------------
// EndpointCatalogue interface
//-----------------------------------------------------
class EndpointCatalogue
{
public:
    virtual ~EndpointCatalogue() = default;

    virtual void    stopTimers() = 0;

    virtual PeerKey publicKey() = 0;

    virtual void    setEndpointHandler( ::sirius::drive::EndpointHandler endpointHandler ) = 0;

    virtual OptionalEndpoint getEndpoint( const PeerKey& key ) = 0;
    virtual OptionalEndpoint dbgGetEndpointLocal(const PeerKey& key ) = 0;
    virtual OptionalEndpoint dbgGetEndpointHashTable(const PeerKey& key ) = 0;

    virtual const PeerInfo* getPeerInfoSkippingLocalMap( const PeerKey& key ) = 0;

    virtual void            addClientToLocalEndpointMap( const Key& keys ) = 0;
    virtual void            onEndpointDiscovered( const Key& key, const OptionalEndpoint& endpoint ) = 0;

    // 'get-my-ip'
    virtual std::string     onGetMyIpRequest( const std::string& request, boost::asio::ip::udp::endpoint requesterEndpoint ) = 0;
    virtual void            onGetMyIpResponse( const std::string&, boost::asio::ip::udp::endpoint requesterEndpoint ) = 0;

    // 'get-ip'
    virtual std::string     onGetPeerIpRequest( const std::string&, boost::asio::ip::udp::endpoint requesterEndpoint ) = 0;
    
    virtual void            onGetPeerIpResponse( const std::string&, boost::asio::ip::udp::endpoint requesterEndpoint ) = 0;
    
    virtual void            addReplicatorKey( const Key& key ) = 0;
    virtual void            addReplicatorKeys( const std::vector<Key>& keys ) = 0;
    virtual void            removeReplicatorKey( const Key& keys ) = 0;
    
    virtual void            dbgTestKademlia( KademliaDbgFunc dbgFunc ) = 0;
};


} // namespace kademlia

class Session;
std::shared_ptr<kademlia::EndpointCatalogue> createEndpointCatalogue(
                                                    std::weak_ptr<kademlia::Transport>  kademliaTransport,
                                                    const crypto::KeyPair&              keyPair,
                                                    const std::vector<ReplicatorInfo>&  bootstraps,
                                                    uint16_t                            myPort,
                                                    bool                                isClient );

}}


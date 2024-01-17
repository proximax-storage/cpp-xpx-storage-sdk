#pragma once

#include <vector>
#include <cassert>
#include <queue>

#include "drive/log.h"

namespace sirius { namespace drive { namespace kademlia {

#define USE_CLOSEST_NODES_SET


const size_t    BUCKET_NUMBER = sizeof(Key)*8; // 32*8
const size_t    BUCKET_SIZE   = 4;

// Every 15 minutes (CHECK_EXPIRED_SEC) will be checked all peerInfo
// If time is exceed 1 hour (PEER_UPDATE_SEC), "get-peer-info" mesage will be send to this peer
// If time is exceed 2 hour (EXPIRED_SEC), peer-info will be removed
//const
inline uint64_t  CHECK_EXPIRED_SEC   = 15*60;
//const
inline uint64_t  PEER_UPDATE_SEC     = 40*60;
//const
inline uint64_t  EXPIRED_SEC         = 2*60*60;


inline bool isPeerInfoExpired( uint64_t t )
{
    uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    //TODO? check staing logs!!! ??? 20185094 > 49200191+7200
    ___LOG( "isPeerInfoExpired: " << now << " > " << t << "+" << EXPIRED_SEC )
    return now > t+EXPIRED_SEC;
}

inline bool shouldPeerInfoBeUpdated( uint64_t t )
{
    auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    return now-t > PEER_UPDATE_SEC;
}

size_t equalPrefixLength( PeerKey aKey, PeerKey bKey )
{
    static_assert( sizeof(PeerKey)==32, "sizeof(PeerKey)==32" );
    using PeerKey64 = std::array<uint64_t,4>;

    PeerKey64& a = reinterpret_cast<PeerKey64&>(aKey);
    PeerKey64& b = reinterpret_cast<PeerKey64&>(bKey);

    size_t index = 0;
    for( size_t i=0; i<a.size(); i++ )
    {
        auto xorValue = a[i] ^ b[i];
        for( int j=0; j<8; j++ )
        {
            if ( xorValue&0x01 )
            {
                return index;
            }
            xorValue = xorValue>>1;
            index++;
        }
    }
    
    _SIRIUS_ASSERT(aKey!=bKey );
    return BUCKET_NUMBER-1;
};

class Bucket
{
    std::vector<PeerInfo> m_nodes;
public:
    
    Bucket() { m_nodes.reserve( BUCKET_SIZE ); }
    
    bool empty() const { return m_nodes.empty(); }
    
    std::vector<PeerInfo>&       nodes() { return m_nodes; }

    const std::vector<PeerInfo>& nodes() const { return m_nodes; }

    void removeExpiredNodes()
    {
        std::erase_if( m_nodes, [] (auto& peerInfo)
        {
            return isPeerInfoExpired( peerInfo.m_creationTimeInSeconds );
        });
    }

    size_t size() const { return m_nodes.size(); }

    const PeerInfo* getPeer( const PeerKey& key ) const
    {
        for( auto& nodeInfo : m_nodes )
        {
            if ( nodeInfo.m_publicKey == key )
                return &nodeInfo;
        }
        return nullptr;
    }

    bool addPeerOrUpdate( const PeerInfo& info )
    {
        auto it = std::find_if( m_nodes.begin(), m_nodes.end(), [&key=info.m_publicKey] (const auto& item) {
            return key==item.m_publicKey;
        });
        
        if ( it != m_nodes.end() )
        {
            // peer found
            if ( info.m_creationTimeInSeconds > it->m_creationTimeInSeconds )
            {
                // refresh
                *it = info;
            }
            return true;
        }
        else
        {
            // peer not found
            if ( m_nodes.size() < BUCKET_SIZE )
            {
                m_nodes.push_back(info);
                return true;
            }
        }
        return false;
    }
};

}}}

#pragma once

#include <vector>
#include <cassert>
#include <queue>

#include "drive/log.h"

namespace sirius { namespace drive { namespace kademlia {

#define USE_CLOSEST_NODES_SET


const size_t    BUCKET_NUMBER = sizeof(Key)*8; // 32*8
const size_t    BUCKET_SIZE   = 4;

const uint64_t  EXPIRED_SEC         = 2*60*60; // 2 hour
const uint64_t  UPDATE_EXPIRED_SEC  = 60*60;

inline bool isExpired( uint64_t t )
{
    auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    return now-t > EXPIRED_SEC;
}

size_t equalPrefixLength( PeerKey aKey, PeerKey bKey )
{
    static_assert( sizeof(PeerKey)==32, "sizeof(PeerKey)==32" );
    using PeerKey64 = std::array<uint64_t,4>;

    PeerKey64& a = reinterpret_cast<PeerKey64&>(aKey);
    PeerKey64& b = reinterpret_cast<PeerKey64&>(bKey);

    size_t index = 0;
    for( int i=0; i<a.size(); i++ )
    {
        auto xorValue = a[i] ^ b[i];
        for( int i=0; i<8; i++ )
        {
            if ( xorValue&0x01 )
            {
                return index;
            }
            xorValue = xorValue>>1;
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
    
    const std::vector<PeerInfo>& nodes() const { return m_nodes; }

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

    void addPeer( const PeerInfo& info )
    {
        auto it = std::find_if( m_nodes.begin(), m_nodes.end(), [&key=info.m_publicKey] (const auto& item) {
            return key==item.m_publicKey;
        });
        if (it != m_nodes.end() )
        {
            if ( info.m_timeInSeconds > it->m_timeInSeconds )
            {
                // refresh
                *it = info;
            }
        }
        else
        {
            if ( m_nodes.size() < BUCKET_SIZE )
            {
                m_nodes.push_back(info);
            }
            
        }
    }

//    bool justFindNode( const PeerKey& searchedKey, bool& isFull ) const
//    {
//        isFull = m_nodes.size() >= BUCKET_SIZE;
//
//        for( auto& nodeInfo : m_nodes )
//        {
//            if ( nodeInfo.m_publicKey == searchedKey )
//                return true;
//        }
//        return false;
//    }
    
//    inline bool findNodeInBucket( const PeerKey& searchedKey ) const
//    {
//        for( auto& nodeInfo : m_nodes )
//        {
//            if ( nodeInfo.m_publicKey == searchedKey )
//                return true;
//        }
//        return false;
//    }
    
//    inline void tryToAddNodeInfo( const PeerKey& requesterPeerKey, NodeIndex index )
//    {
//        if ( m_nodes.size() < BUCKET_SIZE )
//        {
//            for( auto& nodeInfo : m_nodes )
//            {
//                if ( nodeInfo.m_publicKey == requesterPeerKey.m_publicKey )
//                    return;
//            }
//
//            m_nodes.push_back( NodeInfo{ requesterPeerKey.m_publicKey, index } );
//        }
//    }
    
//    inline bool findPeerKey( const PeerKey& searchedPeerKey ) const
//    {
//        for( auto& nodeInfo : m_nodes )
//        {
//            if ( nodeInfo.m_publicKey == searchedPeerKey )
//                return true;
//        }
//        return false;
//    }
    
    
//    inline void addClosestNodes( const PeerKey& searchedPeerKey, ClosestNodes& closestNodes, size_t& addedClosestNodeCounter ) const
//    {
//        static auto rng = std::default_random_engine {};
//#ifdef SORT_CLOSEST_NODES_IN BUCKET
//        //        std::vector<NodeInfo> nodes(m_nodes);
//        std::vector<NodeInfo> nodes;
//        for( const auto& nodeInfo : m_nodes )
//        {
//            nodes.emplace_back( NodeInfo{ nodeInfo.m_publicKey ^ searchedPeerKey.m_publicKey, nodeInfo.m_nodeIndex } );
//        }
//        //        if ( ! nodes.empty() )
//        //        {
//        //            { NodeInfo n = nodes.front(); nodes.erase( nodes.begin() ); nodes.push_back(n); }
//        //            { NodeInfo n = nodes.front(); nodes.erase( nodes.begin() ); nodes.push_back(n); }
//        //        }
//        //        std::shuffle(std::begin(nodes), std::end(nodes), rng);
//        //        std::sort( nodes.begin(), nodes.end() );
//        //        std::reverse( nodes.begin(), nodes.end() );
//        for( const auto& nodeInfo : nodes )
//        {
//            if ( addedClosestNodeCounter >= CLOSEST_NODES_NUMBER )
//            {
//                return;
//            }
//            
//            addedClosestNodeCounter++;
//            closestNodes.push_back( nodeInfo.m_nodeIndex );
//        }
//#else
//        for( const auto& nodeInfo : m_nodes )
//        {
//            if ( addedClosestNodeCounter >= CLOSEST_NODES_NUMBER )
//            {
//                return;
//            }
//            
//            addedClosestNodeCounter++;
//            closestNodes.push_back( nodeInfo.m_nodeIndex );
//        }
//#endif
//    }
};

}}}

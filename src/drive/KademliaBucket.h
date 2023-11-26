#pragma once

#include <vector>
#include <cassert>
#include <queue>

//#include "Utils.h"
//#include "Constants.h"

namespace sirius { namespace drive { namespace kademlia {

#define USE_CLOSEST_NODES_SET


const size_t BUCKET_SIZE = sizeof(Key)*4;


class Bucket
{
    std::vector<PeerInfo> m_nodes;
public:
    
    Bucket() { m_nodes.reserve( BUCKET_SIZE ); }
    
    bool empty() const { return m_nodes.empty(); }
    
    size_t size() const { return m_nodes.size(); }
    
    const std::vector<PeerInfo>& nodes() const { return m_nodes; }

    const PeerInfo* getPeerInfo( const PeerKey& key ) const
    {
        for( auto& nodeInfo : m_nodes )
        {
            if ( nodeInfo.m_publicKey == key )
                return &nodeInfo;
        }
        return nullptr;
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

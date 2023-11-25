#pragma once
#include <iostream>
#include <vector>
#include <deque>
#include <array>
#include <set>

#include "KademliaBucket.h"
//#include "Utils.h"

namespace sirius { namespace drive { namespace kademlia {

#define USE_CLOSEST_NODES_SET

inline int equalPrefixLength( const PeerKey& a, const PeerKey& b )
{
    return 0;
}



class HashTable //: public PeerKey, public NodeStatistic
{
    PeerKey m_key;
    std::array<Bucket,BUCKET_SIZE> m_buckets;
    
//#ifdef USE_CLOSEST_NODES_SET
//    std::set<NodeInfo>  m_candidateSet;
//    std::set<PeerKey>       m_usedCandidates;
//#endif
    
public:
    HashTable(){}
    
    HashTable( const PeerKey& key )
    {
        m_key = key;
    }
    
    const PeerKey& key() const { return m_key; }
    
    int calcBucketIndex( const PeerKey& candidate ) const
    {
        return equalPrefixLength( this->m_key, candidate );
    }
    
    std::optional<boost::asio::ip::udp::endpoint> getPeerInfo( const PeerKey& key, Bucket* bucket )
    {
        auto bucketIndex = 0; //TODO? calcBucketIndex( key );
        
        const PeerInfo* info = m_buckets[bucketIndex].getPeerInfo( key );
        if ( info != nullptr )
            return info->endpoint();
        
        if ( ! m_buckets[bucketIndex].nodes().empty() )
        {
            bucket = &m_buckets[bucketIndex];
            return;
        }
        
        //TODO? up/down algorithm
        
        return {};
    }
    
    bool justFind( const PeerKey& searchedNodeKey, int& bucketIndex, bool& isFull )
    {
        bucketIndex = calcBucketIndex( searchedNodeKey );
        
        return m_buckets[bucketIndex].justFindNode( searchedNodeKey, isFull );
    }
    
    bool justFindNodeInBuckets( const PeerKey& searchedNodeKey )
    {
        int index = calcBucketIndex( searchedNodeKey );
        //LOG( " this: " << this << " b_index: " << index << " key: " << searchedNodeKey.m_key )
        return m_buckets[index].findNodeInBucket(searchedNodeKey);
    }
    
//    bool privateFindNode( const PeerKey& searchedNodeKey, ClosestNodes& closestNodes, const Node& requesterNode )
//    {
//        // skip myself
//        if ( requesterNode.m_key != m_key )
//        {
//            //
//            // Always try to add requester to my 'Buckets'
//            //
//            int index = calcBucketIndex( requesterNode );
//            m_buckets[index].tryToAddNodeInfo( requesterNode, requesterNode.m_index );
//        }
//
//        int index = calcBucketIndex( searchedNodeKey );
//
//        if ( m_buckets[index].findNodeKey( searchedNodeKey ) )
//        {
//            return true;
//        }
//
//        size_t addedClosestNodeCounter = 0;
//        m_buckets[index].addClosestNodes( searchedNodeKey, closestNodes, addedClosestNodeCounter );
//
//        {
//            auto i = index;
//            while( addedClosestNodeCounter < CLOSEST_NODES_NUMBER && i > 0 )
//            {
//                i--;
//                m_buckets[i].addClosestNodes( searchedNodeKey, closestNodes, addedClosestNodeCounter );
//            }
//        }
//
//        {
//            auto i = index;
//            while( addedClosestNodeCounter < CLOSEST_NODES_NUMBER && i < m_buckets.size()-1 )
//            {
//                i++;
//                m_buckets[i].addClosestNodes( searchedNodeKey, closestNodes, addedClosestNodeCounter );
//            }
//        }
//
//        return false;
//    }
    
//    void enterToSwarm( Node& bootstrapNode, bool enterToSwarm = false )
//    {
//        ClosestNodes closestNodes;
//
//        // query 'bootstrapNode' for closest node list
//        bootstrapNode.privateFindNode( *this, closestNodes, *this );
//
//        continueFindNode( *this, closestNodes );
//    }
    
//    inline bool addNodeToBuckets( const Node& node )
//    {
//        assert( node.m_key != m_key );
//        int index = calcBucketIndex( node );
//        m_buckets[index].tryToAddNodeInfo( node, node.m_index );
//    }
    
//    bool findNode( const PeerKey& searchedNodeKey )
//    {
//        ClosestNodes closestNodes;
//#ifdef USE_CLOSEST_NODES_SET
//        m_candidateSet.clear();
//        m_usedCandidates.clear();
//#endif
//
//        m_isFound = privateFindNode( searchedNodeKey, closestNodes, *this );
//
//        if ( ! m_isFound )
//        {
//            m_isFound = continueFindNode( searchedNodeKey, closestNodes );
//        }
//
//        return m_isFound;
//    }
    
//    bool continueFindNode( const PeerKey& searchedNodeKey, ClosestNodes& closestNodes )
//    {
//        m_requestCounter = 0;
//
//#ifdef USE_CLOSEST_NODES_SET
//        while( ++m_requestCounter <= MAX_FIND_COUNTER )
//        {
//            assert( closestNodes.size() <= CLOSEST_NODES_NUMBER );
//            for( size_t i=0; i<closestNodes.size(); i++ )
//            {
//                Node& closestNode = *((this-m_index) + closestNodes[i]);
//                if ( ! m_usedCandidates.contains( closestNode.m_key ) )
//                {
//                    m_candidateSet.emplace( NodeInfo{ closestNode.m_key ^ searchedNodeKey.m_key, closestNode.m_index } );
//                }
//            }
//            closestNodes.clear();
//
//            if ( m_candidateSet.empty() )
//            {
//                return false;
//            }
//
//            NodeInfo info = * m_candidateSet.begin();
//            m_candidateSet.erase( m_candidateSet.begin() );
//            m_usedCandidates.emplace( info.m_key ^ searchedNodeKey.m_key );
//
//            Node& closestNode = *((this-m_index) + info.m_nodeIndex);
//
//            if ( closestNode.privateFindNode( searchedNodeKey, closestNodes, *this ) )
//            {
//                addClosestNodeToBuckets( closestNode );
//                return true;
//            }
//            if ( closestNodes.size() > 0 )
//            {
//                addClosestNodeToBuckets( closestNode );
//            }
//        }
//#else
//        for( size_t i=0; i<closestNodes.size(); i++ )
//        {
//            if ( ++m_requestCounter > MAX_FIND_COUNTER )
//            {
//                return false;
//            }
//
//            Node& closestNode = *((this-m_index) + closestNodes[i]);
//
//            if ( closestNode.privateFindNode( searchedNodeKey, closestNodes, requesterNode ) )
//            {
//                //addClosestNodeToBuckets( closestNode );
//                return true;
//            }
//            addClosestNodeToBuckets( closestNode );
//        }
//#endif
//
//        return false;
//    }
    
//    void addClosestNodeToBuckets( Node& closestNode )
//    {
//        int index = calcBucketIndex( closestNode );
//        if ( index < sizeof(m_buckets)/sizeof(m_buckets[0]) )
//        {
//            m_buckets[index].tryToAddNodeInfo( closestNode, closestNode.m_index );
//        }
//    }
    
    
    size_t nodeCount() const
    {
        size_t nodeCount = 0;
        for( const Bucket& b: m_buckets )
        {
            nodeCount += b.size();
        }
        return nodeCount;
    }
};

}}}

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
    //TODO?
    return 0;
}



class KademliaHashTable //: public PeerKey, public NodeStatistic
{
    PeerKey m_key;
    std::array<Bucket,BUCKET_SIZE> m_buckets;
    
//#ifdef USE_CLOSEST_NODES_SET
//    std::set<NodeInfo>  m_candidateSet;
//    std::set<PeerKey>       m_usedCandidates;
//#endif
    
public:
    KademliaHashTable(){}
    
    KademliaHashTable( const PeerKey& key )
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
        auto bucketIndex = equalPrefixLength( m_key, key );
        
        const PeerInfo* info = m_buckets[bucketIndex].getPeerInfo( key );
        if ( info != nullptr )
        {
            return info->endpoint();
        }
        
        if ( ! m_buckets[bucketIndex].nodes().empty() )
        {
            bucket = &m_buckets[bucketIndex];
            return {};
        }
        
        //TODO? up/down algorithm
        
        return {};
    }
    
    // onSearchPeerInfo() is used for request from another peer
    //
    std::vector<PeerInfo> onSearchPeerInfo( const PeerKey& key )
    {
        auto bucketIndex = 0; //TODO? calcBucketIndex( key );

        const PeerInfo* info = m_buckets[bucketIndex].getPeerInfo( key );
        if ( info != nullptr )
        {
            return std::vector<PeerInfo>{ *info };
        }
        
        //TODO? up/down algorithm
        
        return {};
    }
    
//    bool justFind( const PeerKey& searchedNodeKey, int& bucketIndex, bool& isFull )
//    {
//        bucketIndex = calcBucketIndex( searchedNodeKey );
//
//        return m_buckets[bucketIndex].justFindNode( searchedNodeKey, isFull );
//    }
//
//    bool justFindNodeInBuckets( const PeerKey& searchedNodeKey )
//    {
//        int index = calcBucketIndex( searchedNodeKey );
//        //LOG( " this: " << this << " b_index: " << index << " key: " << searchedNodeKey.m_key )
//        return m_buckets[index].findNodeInBucket(searchedNodeKey);
//    }
//
//    size_t nodeCount() const
//    {
//        size_t nodeCount = 0;
//        for( const Bucket& b: m_buckets )
//        {
//            nodeCount += b.size();
//        }
//        return nodeCount;
//    }
};

}}}

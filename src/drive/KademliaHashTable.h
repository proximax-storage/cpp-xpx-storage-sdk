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

class KademliaHashTable //: public PeerKey, public NodeStatistic
{
    PeerKey m_myKey;
    std::array<Bucket,BUCKET_NUMBER> m_buckets;
    
//#ifdef USE_CLOSEST_NODES_SET
//    std::set<NodeInfo>  m_candidateSet;
//    std::set<PeerKey>       m_usedCandidates;
//#endif
    
public:
    KademliaHashTable(){}
    
    KademliaHashTable( const PeerKey& key )
    {
        m_myKey = key;
    }
    
    const PeerKey& key() const { return m_myKey; }
    
    const std::array<Bucket,BUCKET_NUMBER>& buckets() const { return m_buckets; }

    int calcBucketIndex( const PeerKey& candidate ) const
    {
        return equalPrefixLength( m_myKey, candidate );
    }
    
    std::optional<boost::asio::ip::udp::endpoint> getPeerInfo( const PeerKey& key, size_t& bucketIndex )
    {
        bucketIndex = equalPrefixLength( m_myKey, key );
        
        const PeerInfo* info = m_buckets[bucketIndex].getPeer( key );
        if ( info != nullptr )
        {
            return info->endpoint();
        }
        
        if ( ! m_buckets[bucketIndex].nodes().empty() )
        {
            return {};
        }
        
        //TODO? up/down algorithm
        
        return {};
    }
    
    // onRequestFromAnotherPeer() is used for request from another peer
    //
    std::vector<PeerInfo> onRequestFromAnotherPeer( const PeerKey& searchedKey )
    {
        auto bucketIndex = calcBucketIndex( searchedKey );

        const PeerInfo* info = m_buckets[bucketIndex].getPeer( searchedKey );
        if ( info != nullptr )
        {
            return std::vector<PeerInfo>{ *info };
        }
        
        //TODO? up/down algorithm
        
        return {};
    }
    
    void addPeerInfo( const PeerInfo& info )
    {
        auto bucketIndex = calcBucketIndex( info.m_publicKey );
        m_buckets[bucketIndex].addPeer( info );
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

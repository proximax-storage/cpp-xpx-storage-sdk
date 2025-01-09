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
//    KademliaHashTable(){}
    
    KademliaHashTable( const PeerKey& key ) : m_myKey(key) {
        
    }
    
    const PeerKey& key() const { return m_myKey; }
    
          std::array<Bucket,BUCKET_NUMBER>& buckets()       { return m_buckets; }
    
    const std::array<Bucket,BUCKET_NUMBER>& buckets() const { return m_buckets; }
    
    int calcBucketIndex( const PeerKey& candidate ) const
    {
        return (int) equalPrefixLength( m_myKey, candidate );
    }
    
    const PeerInfo* getPeerInfo( const PeerKey& key, size_t& bucketIndex )
    {
        bucketIndex = equalPrefixLength( m_myKey, key );
        
        return m_buckets[bucketIndex].getPeer( key );
    }
    
    size_t calcBucketIndex( const PeerKey& key )
    {
        return equalPrefixLength( m_myKey, key );
    }
    
    // onRequestFromAnotherPeer() is used for request from another peer
    //
    std::vector<PeerInfo> findClosestNodes( const PeerKey& searchedKey )
    {
        std::vector<PeerInfo> result;
        
        auto bucketIndex = calcBucketIndex( searchedKey );
        
        const PeerInfo* info = m_buckets[bucketIndex].getPeer( searchedKey );
        if ( info != nullptr )
        {
            result.push_back( *info );
            return result;
        }
        
        for( const auto& info : m_buckets[bucketIndex].nodes() )
        {
            result.push_back(info);
        }
        
        // up
        auto bucketI = bucketIndex;
        while( result.size() < BUCKET_SIZE && (bucketI > 0) )
        {
            bucketI--;
            
            for( const auto& info : m_buckets[bucketI].nodes() )
            {
                if( result.size() >= BUCKET_SIZE)
                {
                    break;
                }
                result.push_back(info);
            }
        }
        
        // down
        bucketI = bucketIndex;
        while( result.size() < BUCKET_SIZE && (bucketI < BUCKET_NUMBER-1) )
        {
            bucketI++;

            for( const auto& info : m_buckets[bucketI].nodes() )
            {
                if( result.size() >= BUCKET_SIZE)
                {
                    break;
                }
                result.push_back(info);
            }
        }
        
        return result;
    }
    
    int addPeerInfoOrUpdate( const PeerInfo& info )
    {
        if ( info.m_publicKey == m_myKey )
        {
            return -1;
        }
        
        auto bucketIndex = (int)calcBucketIndex( info.m_publicKey );
        //___LOG( "bucketIndex: " << bucketIndex << " " << info.m_publicKey << " " << m_myKey )
        if ( ! m_buckets[bucketIndex].addPeerOrUpdate( info ) )
        {
            return -bucketIndex-1;
        }
        return bucketIndex;
    }

    bool couldBeAdded( const PeerKey& key )
    {
        size_t bucketIndex;
        if ( ! getPeerInfo( key, bucketIndex ) )
        {
            if ( m_buckets[bucketIndex].nodes().size() < BUCKET_NUMBER )
            {
                return true;
            }
        }
        return false;
    }
        
    void removePeerInfo( const Key& key )
    {
        auto bucketIndex = calcBucketIndex( key );
        auto bucketNodes = m_buckets[bucketIndex].nodes();
        std::erase_if( bucketNodes, [&key] (const auto& peerInfo) -> bool {
            return peerInfo.m_publicKey == key;
        });
    }

};

}}}

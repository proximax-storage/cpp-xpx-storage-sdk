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
    
    // onRequestFromAnotherPeer() is used for local requests only
    //
    std::optional<boost::asio::ip::udp::endpoint> getPeerInfo( const PeerKey& key, size_t& bucketIndex )
    {
        bucketIndex = equalPrefixLength( m_myKey, key );
        
        const PeerInfo* info = m_buckets[bucketIndex].getPeer( key );
        if ( info != nullptr )
        {
            return info->endpoint();
        }
        
        return {};
    }
    
    // onRequestFromAnotherPeer() is used for request from another peer
    //
    std::vector<PeerInfo> onRequestFromAnotherPeer( const PeerKey& searchedKey )
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
        while( result.size() < BUCKET_SIZE && (bucketI < BUCKET_NUMBER) )
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
    
    void addPeerInfo( const PeerInfo& info )
    {
        auto bucketIndex = calcBucketIndex( info.m_publicKey );
        m_buckets[bucketIndex].addPeer( info );
    }

    bool couldBeAdded( const PeerKey& key )
    {
        auto bucketIndex = calcBucketIndex( key );

        return m_buckets[bucketIndex].nodes().size() < BUCKET_NUMBER;
    }
        
};

}}}

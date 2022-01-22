/*
*** Copyright 2022 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include "types.h"
#include "drive/FlatDrive.h"
//#include "crypto/Signer.h"
#include "drive/Session.h"

#include <array>
#include <map>

namespace sirius::drive {

class DnOpinionSyncronizer
{
private:
    using Timer         = boost::asio::high_resolution_timer;

    using ReplicatorKey = std::array<uint8_t,32>;
    using OpinionMap    = std::map<ReplicatorKey,mobj<DownloadOpinion>>;
    
    struct ChannelSyncInfo
    {
        std::shared_ptr<FlatDrive>  m_drive;
        size_t                      m_consensusThreshould;
        OpinionMap                  m_opinionMap;
        std::optional<Timer>        m_syncTimeoutTimer;
    };
    
    using ChannelId     = std::array<uint8_t,32>;
    using ChannelMap    = std::map<ChannelId,ChannelSyncInfo>;

public:
    ChannelMap               m_channelMap;
    Replicator&              m_replicator;
    std::shared_ptr<Session> m_session;

public:
    DnOpinionSyncronizer( Replicator& replicator ) : m_replicator(replicator) {}
    ~DnOpinionSyncronizer() {}
    
    void start( std::shared_ptr<Session>& session )
    {
        m_session = session;
    }
    
    void stop()
    {
        m_channelMap.clear();
        m_session.reset();
    }
    
    void startSync( const ChannelId& channelId, std::shared_ptr<FlatDrive>& drive, size_t consensusThreshould )
    {
        auto channelIt = m_channelMap.lower_bound( channelId );

        if ( channelIt == m_channelMap.end() )
        {
            channelIt = m_channelMap.insert( channelIt, { channelId, ChannelSyncInfo{drive,consensusThreshould,{},{}} } );
        }
        
        auto& channelSyncInfo = channelIt->second;
        requestOpinons( channelIt->first, channelSyncInfo );
        return;
    }

    void requestOpinons( const ChannelId& channelId, ChannelSyncInfo& channelSyncInfo )
    {
        // request receipts from neighbors replicators
        //
        for( auto key: channelSyncInfo.m_drive->replicatorList() )
        {
            // do not skip existing opinions
            //if ( channelSyncInfo.m_opinionMap.find(key.array()) == channelSyncInfo.m_opinionMap.end() )
            {
                std::vector<uint8_t> message;
                auto& driveKey = channelSyncInfo.m_drive->drivePublicKey();
                message.insert( message.end(), driveKey.begin(), driveKey.end() );
                message.insert( message.end(), channelId.begin(), channelId.end() );
                m_replicator.sendMessage( "get_dn_rcpts", key.array(), message );
            }
        }
        
        channelSyncInfo.m_syncTimeoutTimer = m_session->startTimer( 1, [&,this]{
            requestOpinons( channelId, channelSyncInfo );
            channelSyncInfo.m_syncTimeoutTimer.reset();
        } );
    }
    
    void addSyncOpinion( const ChannelId& channelId, mobj<DownloadOpinion>&& opinion )
    {
        auto channelIt = m_channelMap.lower_bound( channelId );

        if ( channelIt == m_channelMap.end() )
        {
            // ignore unknown opinion
            return;
        }

        auto opinionIt = channelIt->second.m_opinionMap.find( opinion->m_replicatorKey );
        if ( opinionIt != channelIt->second.m_opinionMap.end() )
        {
            // replace opinion
            opinionIt->second = std::move(opinion);
        }
        else
        {
            channelIt->second.m_opinionMap.insert( opinionIt, { opinion->m_replicatorKey, std::move(opinion) } );
            
            if ( channelIt->second.m_opinionMap.size() >= channelIt->second.m_consensusThreshould )
            {
                struct cell{ uint64_t m_downloadedSize = 0; int opinonNumber = 0;};
                
                std::map<std::array<uint8_t,32>,int>        frequnceMap;
                std::map<std::array<uint8_t,32>,uint64_t>   downloadMap;
                
                for( auto& opinion: channelIt->second.m_opinionMap )
                {
                    for( auto& keyAndBytes: opinion.second->m_downloadLayout )
                    {
                        frequnceMap[keyAndBytes.m_key] ++;
                        downloadMap[keyAndBytes.m_key] += keyAndBytes.m_uploadedBytes;
                    }
                }

                for( auto& [key,value]: downloadMap )
                {
                    auto freq = frequnceMap[key];
                    if ( freq > 0 )
                        value = value / freq;
                }
            }
        }
    }

};

} //namespace sirius::drive {

/*
*** Copyright 2022 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include "types.h"
#include "drive/FlatDrive.h"
#include "Session.h"

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
    
    using SyncChannelMap = std::map<ChannelId,ChannelSyncInfo>;

public:
    SyncChannelMap           m_syncChannelMap;
    ReplicatorInt&              m_replicator;
    std::shared_ptr<Session> m_session;

    std::string              m_dbgOurPeerName;

public:
    DnOpinionSyncronizer( ReplicatorInt& replicator ) : m_replicator(replicator), m_dbgOurPeerName(replicator.dbgReplicatorName()) {}
    ~DnOpinionSyncronizer() {}
    
    void start( std::shared_ptr<Session>& session )
    {
        m_session = session;
    }
    
    void stop()
    {
        m_syncChannelMap.clear();
        m_session.reset();
    }
    
    void startSync( const ChannelId& channelId, std::shared_ptr<FlatDrive>& drive, size_t consensusThreshould )
    {
        auto syncChannelIt = m_syncChannelMap.lower_bound( channelId );

        if ( syncChannelIt == m_syncChannelMap.end() )
        {
            syncChannelIt = m_syncChannelMap.insert( syncChannelIt, { channelId, ChannelSyncInfo{drive,consensusThreshould,{},{}} } );
        }
        
        auto& channelSyncInfo = syncChannelIt->second;
        requestOpinons( syncChannelIt->first, channelSyncInfo );
        return;
    }

    void requestOpinons( const ChannelId& channelId, ChannelSyncInfo& channelSyncInfo )
    {
        // request receipts from neighbors replicators
        //
        for( auto key: channelSyncInfo.m_drive->getAllReplicators() )
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
    
    void addSyncOpinion( const ChannelId& channelId, mobj<DownloadOpinion>&& opinion, ChannelMap& channelMap )
    {
        //(???+++)
//        auto syncChannelIt = m_syncChannelMap.find( channelId );
//
//        if ( syncChannelIt == m_syncChannelMap.end() )
//        {
//            _LOG_WARN( "Unknown sync channel id" << Key(channelId) )
//            return;
//        }
//
//        auto opinionIt = syncChannelIt->second.m_opinionMap.find( opinion->m_replicatorKey );
//        if ( opinionIt != syncChannelIt->second.m_opinionMap.end() )
//        {
//            // replace opinion
//            opinionIt->second = std::move(opinion);
//        }
//        else
//        {
//            syncChannelIt->second.m_opinionMap.insert( opinionIt, { opinion->m_replicatorKey, std::move(opinion) } );
//
//            _LOG( "syncChannelIt->second.m_opinionMap.size()=" << syncChannelIt->second.m_opinionMap.size() )
//            if ( syncChannelIt->second.m_opinionMap.size() >= syncChannelIt->second.m_consensusThreshould )
//            {
//                //struct cell{ uint64_t m_downloadedSize = 0; int opinonNumber = 0;};
//
//                std::map<std::array<uint8_t,32>,int>        frequnceMap;
//                std::map<std::array<uint8_t,32>,uint64_t>   medianDownloadMap;
//
//                // Calculate cummulative opinion values
//                //
//                for( auto& opinion: syncChannelIt->second.m_opinionMap )
//                {
//                    for( auto& keyAndBytes: opinion.second->m_downloadLayout )
//                    {
//                        frequnceMap[keyAndBytes.m_key]++;
//                        medianDownloadMap[keyAndBytes.m_key] += keyAndBytes.m_uploadedBytes;
//                    }
//                }
//
//                // Normalize
//                //
//                for( auto& [key,downloadValueRef]: medianDownloadMap )
//                {
//                    auto& freq = frequnceMap[key];
//                    if ( freq > 0 )
//                        downloadValueRef = downloadValueRef / freq;
//                }
//
//                // Update download channel info
//                //
//                if ( auto channelInfoIt = channelMap.find(channelId); channelInfoIt != channelMap.end() )
//                {
//                    auto& replicatorUploadMap = channelInfoIt->second.m_replicatorUploadMap;
//                    for( const auto& [replicatorKey,downloadValue] : medianDownloadMap )
//                    {
//                        _LOG( "replicatorKey,downloadValue: " << int(replicatorKey[0]) << ", " << downloadValue )
//                        auto it = replicatorUploadMap.lower_bound( replicatorKey );
//                        if ( it == replicatorUploadMap.end() )
//                        {
//                            replicatorUploadMap.insert( it, { replicatorKey, {downloadValue} } );
//                        }
//                        else
//                        {
//                            if ( it->second.m_uploadedSize < downloadValue )
//                            {
//                                it->second.m_uploadedSize = downloadValue;
//                            }
//                        }
//                        channelInfoIt->second.m_isSyncronizing = false;
//                    }
//                }
//                else
//                {
//                    _LOG_WARN( "Unknown channel id" << Key(channelId) )
//                    return;
//                }
//
//                // remove sync channel info
//                m_syncChannelMap.erase( syncChannelIt );
//                _LOG( "channel is synced: chId=" << int(channelId[0]) )
//            }
//        }
    }

};

} //namespace sirius::drive {

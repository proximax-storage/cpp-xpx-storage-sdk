/*
*** Copyright 2022 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include "types.h"
#include "drive/FlatDrive.h"
#include "drive/Session.h"

#include <array>
#include <map>

namespace sirius::drive {

class RcptSyncronizer
{
private:
    struct ChannelSyncInfo
    {
        std::shared_ptr<FlatDrive>  m_drive;
        std::set<ReplicatorKey>     m_responseSet;
        size_t                      m_consensusThreshould;
        Timer                       m_syncTimeoutTimer;
    };
    
    using SyncChannelMap = std::map<ChannelId,ChannelSyncInfo>;

public:
    SyncChannelMap           m_syncChannelMap;
    ReplicatorInt&           m_replicator;
    std::shared_ptr<Session> m_session;

    std::string              m_dbgOurPeerName;

public:
    RcptSyncronizer( ReplicatorInt& replicator, const std::string& dbgReplicatorName ) : m_replicator(replicator), m_dbgOurPeerName(dbgReplicatorName) {}
    ~RcptSyncronizer() {}
    
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
            syncChannelIt = m_syncChannelMap.insert( syncChannelIt, { channelId, ChannelSyncInfo{drive,{},consensusThreshould, Timer() } } );
        }
        
        auto& channelSyncInfo = syncChannelIt->second;
        requestOpinons( syncChannelIt->first, channelSyncInfo );
        return;
    }

    void requestOpinons( const ChannelId& channelId, ChannelSyncInfo& channelSyncInfo )
    {
        // request receipts from neighbors replicators
        //
        for( auto replicatorKey: channelSyncInfo.m_drive->getAllReplicators() )
        {
            // do not skip existing opinions
            //if ( channelSyncInfo.m_opinionMap.find(key.array()) == channelSyncInfo.m_opinionMap.end() )
            {
                std::vector<uint8_t> message;
                auto& driveKey = channelSyncInfo.m_drive->drivePublicKey();
                message.insert( message.end(), m_replicator.replicatorKey().begin(), m_replicator.replicatorKey().end() );
                message.insert( message.end(), driveKey.begin(), driveKey.end() );
                message.insert( message.end(), channelId.begin(), channelId.end() );
                m_replicator.sendSignedMessage( "get_dn_rcpts", replicatorKey.array(), message );
            }
        }
        
        //(???+) todo: startTimer( "15*1000" )
        channelSyncInfo.m_syncTimeoutTimer = m_session->startTimer( 15*1000, [&,this]{
            requestOpinons( channelId, channelSyncInfo );
            channelSyncInfo.m_syncTimeoutTimer.cancel();
        } );
    }
    
    void accpeptOpinion( const ChannelId& channelId, const ReplicatorKey& replicatorKey )
    {
        auto syncChannelIt = m_syncChannelMap.find( channelId );

        if ( syncChannelIt == m_syncChannelMap.end() )
        {
            _LOG_WARN( "Unknown sync channel id: " << channelId )
            return;
        }

        syncChannelIt->second.m_responseSet.insert( replicatorKey );
        if ( syncChannelIt->second.m_responseSet.size() >= syncChannelIt->second.m_consensusThreshould )
        {
            syncChannelIt->second.m_syncTimeoutTimer.cancel();
            m_syncChannelMap.erase( syncChannelIt );
            _LOG( "channel is synced: chId=" << int(channelId[0]) )
        }
    }

};

} //namespace sirius::drive {

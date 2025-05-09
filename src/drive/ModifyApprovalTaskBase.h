/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "DownloadLimiter.h"
#include "DriveTaskBase.h"
#include "drive/FsTree.h"
#include "drive/ActionList.h"
#include "drive/FlatDrive.h"
#include "DriveParams.h"
#include "UpdateDriveTaskBase.h"

#include <boost/multiprecision/cpp_int.hpp>

#include <numeric>

#include <cereal/types/vector.hpp>
#include <cereal/types/array.hpp>
#include <cereal/types/map.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/archives/portable_binary.hpp>

namespace sirius::drive
{

namespace fs = std::filesystem;

class ModifyApprovalTaskBase : public UpdateDriveTaskBase
{

protected:

    std::map<std::array<uint8_t,32>,ApprovalTransactionInfo> m_receivedOpinions;

    bool m_modifyApproveTransactionSent = false;
    bool m_modifyApproveTxReceived = false;

    uint64_t m_uploadedDataSize = 0;

    Timer    m_shareMyOpinionTimer;

    const int m_shareMyOpinionTimerDelayMs = 1000 * 60;

    Timer m_modifyOpinionTimer;

protected:

    ModifyApprovalTaskBase(
            const DriveTaskType&    type,
            DriveParams&            drive,
            std::map<std::array<uint8_t,32>,ApprovalTransactionInfo>&&  receivedOpinions,
            ModifyOpinionController&                                    opinionTaskController )
            : UpdateDriveTaskBase( type, drive, opinionTaskController )
    {}


protected:

    void myOpinionIsCreated() override
    {
        DBG_MAIN_THREAD

        SIRIUS_ASSERT( m_myOpinion )

        if ( m_taskIsInterrupted )
        {
            finishTask();
            return;
        }

        m_sandboxCalculated = true;

        if ( m_modifyApproveTxReceived )
        {
            sendSingleApprovalTransaction( *m_myOpinion );
            startSynchronizingDriveWithSandbox();
        } else
        {
            // Send my opinion to other replicators
            shareMyOpinion();

            // validate already received opinions
            std::erase_if( m_receivedOpinions, [this]( const auto& item )
            {
                return !validateOpinion( item.second );
            } );

            // Maybe send approval transaction
            checkOpinionNumberAndStartTimer();
        }
    }

    void shareMyOpinion()
    {
        DBG_MAIN_THREAD

        _LOG( "shareMyOpinion" )

        std::ostringstream os( std::ios::binary );
        cereal::PortableBinaryOutputArchive archive( os );
        archive( *m_myOpinion );

        for ( const auto& replicatorIt : m_drive.getAllReplicators())
        {
			if ( replicatorIt != m_drive.m_replicator.replicatorKey() ) {
				m_drive.m_replicator.sendMessage( "opinion", replicatorIt.array(), os.str());
			}
        }

        if ( auto session = m_drive.m_session.lock(); session )
        {
            m_shareMyOpinionTimer = session->startTimer( m_shareMyOpinionTimerDelayMs, [this]
            {
                shareMyOpinion();
            } );
        }
    }

    void checkOpinionNumberAndStartTimer()
    {
        DBG_MAIN_THREAD

        // m_drive.getReplicator()List is the list of other replicators (it does not contain our replicator)
#ifndef MINI_SIGNATURE
        auto replicatorNumber = (std::max((std::size_t)m_drive.m_replicator.getMinReplicatorsNumber(), m_drive.getAllReplicators().size() + 1) * 2) / 3;
#else
        auto replicatorNumber = ((m_drive.getAllReplicators().size() + 1) * 2) / 3;
#endif

        // check opinion number

        if ( m_myOpinion &&
             m_receivedOpinions.size() >=
             m_drive.getAllReplicators().size() &&
             !m_modifyApproveTransactionSent &&
             !m_modifyApproveTxReceived ) {
            m_modifyOpinionTimer.cancel();
            opinionTimerExpired();
            return;
        }

        if ( m_myOpinion &&
             m_receivedOpinions.size() >= replicatorNumber&&
             !m_modifyApproveTransactionSent &&
             !m_modifyApproveTxReceived )
        {
            // start timer if it is not started
            if ( !m_modifyOpinionTimer )
            {
                if ( auto session = m_drive.m_session.lock(); session )
                {
                    m_modifyOpinionTimer = session->startTimer(
                            m_drive.m_replicator.getModifyApprovalTransactionTimerDelay(),
                            [this]()
                            { opinionTimerExpired(); } );
                }
            }
        }
    }

    void opinionTimerExpired()
    {
        DBG_MAIN_THREAD

        if ( m_modifyApproveTransactionSent || m_modifyApproveTxReceived )
            return;

        ApprovalTransactionInfo info = {m_drive.m_driveKey.array(),
                                        m_myOpinion->m_modifyTransactionHash,
                                        m_myOpinion->m_rootHash,
										m_myOpinion->m_status,
                                        m_myOpinion->m_fsTreeFileSize,
                                        m_myOpinion->m_metaFilesSize,
                                        m_myOpinion->m_driveSize,
                                        {}};

        info.m_opinions.reserve( m_receivedOpinions.size() + 1 );
        info.m_opinions.emplace_back( m_myOpinion->m_opinions[0] );
        for ( const auto& otherOpinion : m_receivedOpinions )
        {
            info.m_opinions.emplace_back( otherOpinion.second.m_opinions[0] );
        }

        // notify event handler
        m_drive.m_eventHandler.modifyApprovalTransactionIsReady( m_drive.m_replicator, info );

        m_modifyApproveTransactionSent = true;
    }

    // updates drive (2st phase after fsTree torrent removed)
    // - remove unused files and torrent files
    // - add new torrents to session
    //
    void continueSynchronizingDriveWithSandbox() override
    {
        DBG_BG_THREAD

        try
        {
            _LOG( "IN UPDATE 2" )

            // update FsTree file & torrent
            if ( ! fs::exists( m_drive.m_sandboxFsTreeFile ) )
            {
                _LOG_ERR( "not exist 1: " << m_drive.m_sandboxFsTreeFile )
            }
            if ( ! fs::exists( m_drive.m_fsTreeFile.parent_path() ) )
            {
                _LOG_ERR( "not exist 2: " <<m_drive.m_fsTreeFile.parent_path() )
            }
            moveFile( m_drive.m_sandboxFsTreeFile, m_drive.m_fsTreeFile );
            moveFile( m_drive.m_sandboxFsTreeTorrent, m_drive.m_fsTreeTorrent );

            //m_drive.m_serializer.saveRestartValue( getModificationTransactionHash().array(), "approvedModification" );

            auto& torrentHandleMap = m_drive.m_torrentHandleMap;
            // remove unused files and torrent files from the drive
            for ( const auto& it : torrentHandleMap )
            {
                const UseTorrentInfo& info = it.second;
                if ( !info.m_isUsed )
                {
                    const auto& hash = it.first;
                    std::string filename = hashToFileName( hash );
                    fs::remove( fs::path( m_drive.m_driveFolder ) / filename );
                    fs::remove( fs::path( m_drive.m_torrentFolder ) / filename );
                }
            }

            // remove unused data from 'fileMap'
            std::erase_if( torrentHandleMap, []( const auto& it )
            { return !it.second.m_isUsed; } );

            //
            // Add torrents into session
            //
            for ( auto& it : torrentHandleMap )
            {
                // load torrent (if it is not loaded)
                //(???+++) unused code
                if ( ! it.second.m_ltHandle.is_valid())
                {
                    if ( auto session = m_drive.m_session.lock(); session )
                    {
                        std::string fileName = hashToFileName( it.first );
                        it.second.m_ltHandle = session->addTorrentFileToSession(
                                m_drive.m_torrentFolder / fileName,
                                m_drive.m_driveFolder,
                                lt::SiriusFlags::peer_is_replicator,
                                &m_drive.m_driveKey.array(),
                                nullptr,
                                nullptr );
                        SIRIUS_ASSERT( it.second.m_ltHandle.is_valid() )
                        _LOG( "downloading: ADDED_TO_SESSION : " << m_drive.m_torrentFolder / fileName )
                    }
                }
            }

            // Add FsTree torrent to session
            if ( auto session = m_drive.m_session.lock(); session )
            {
                m_sandboxFsTreeLtHandle = session->addTorrentFileToSession( m_drive.m_fsTreeTorrent,
                                                                            m_drive.m_fsTreeTorrent.parent_path(),
                                                                            lt::SiriusFlags::peer_is_replicator,
                                                                            &m_drive.m_driveKey.array(),
                                                                            nullptr,
                                                                            nullptr );
            }

            m_drive.executeOnSessionThread( [this]() mutable
                                            {
                                                synchronizationIsCompleted();
                                            } );
        }
        catch (const std::exception& ex)
        {
            _LOG( "exception during updateDrive_2: " << ex.what());
            _LOG_WARN( "exception during updateDrive_2: " << ex.what());
            finishTask();
        }
    }

    bool validateOpinion( const ApprovalTransactionInfo& anOpinion )
    {
        bool equal = m_myOpinion->m_rootHash == anOpinion.m_rootHash &&
					 m_myOpinion->m_status == anOpinion.m_status &&
                     m_myOpinion->m_fsTreeFileSize == anOpinion.m_fsTreeFileSize &&
                     m_myOpinion->m_metaFilesSize == anOpinion.m_metaFilesSize &&
                     m_myOpinion->m_driveSize == anOpinion.m_driveSize;
        return equal;
    }
};

}

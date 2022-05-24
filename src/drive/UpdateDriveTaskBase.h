/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

//#include "DownloadLimiter.h"
//#include "DriveTaskBase.h"
//#include "drive/FsTree.h"
//#include "drive/ActionList.h"
//#include "drive/FlatDrive.h"
#include "DriveParams.h"

//#include <boost/multiprecision/cpp_int.hpp>
//
//#include <numeric>

//#include <cereal/types/vector.hpp>
//#include <cereal/types/array.hpp>
//#include <cereal/types/map.hpp>
//#include <cereal/types/optional.hpp>
//#include <cereal/archives/portable_binary.hpp>

namespace sirius::drive
{

namespace fs = std::filesystem;

class UpdateDriveTaskBase : public DriveTaskBase
{

protected:

    ModifyOpinionController& m_opinionController;

    std::optional<ApprovalTransactionInfo> m_myOpinion;

    mobj<FsTree> m_sandboxFsTree;
    std::optional<InfoHash> m_sandboxRootHash;
    uint64_t m_metaFilesSize = 0;
    uint64_t m_sandboxDriveSize = 0;
    uint64_t m_fsTreeSize = 0;


    std::optional<lt_handle> m_sandboxFsTreeLtHandle;

    std::optional<lt_handle> m_downloadingLtHandle;

    bool m_sandboxCalculated = false;

    bool m_taskIsStopped     = false;

public:

    void onDriveClose( const DriveClosureRequest& closureRequest ) override
    {
        if ( m_taskIsStopped )
        {
            return;
        }

        breakTorrentDownloadAndRunNextTask();
    }

protected:

    virtual const Hash256& getModificationTransactionHash() = 0;

    UpdateDriveTaskBase(
            const DriveTaskType& type,
            DriveParams& drive,
            ModifyOpinionController& opinionTaskController)
            : DriveTaskBase( type, drive )
            , m_opinionController( opinionTaskController )
            , m_sandboxFsTree( FsTree{} )
    {}

    virtual void myRootHashIsCalculated()
    {
        DBG_MAIN_THREAD

        _ASSERT( m_sandboxFsTree )
        _ASSERT( m_sandboxRootHash )

        if ( m_taskIsStopped )
        {
            finishTask();
            return;
        }

        // Notify
        if ( m_drive.m_dbgEventHandler )
        {
            m_drive.m_dbgEventHandler->rootHashIsCalculated(
                    m_drive.m_replicator,
                    m_drive.m_driveOwner,
                    getModificationTransactionHash(),
                    *m_sandboxRootHash );
        }

        /// (???) replace with replicators of the shard
        m_opinionController.updateCumulativeUploads( m_drive.getAllReplicators(), getToBeApprovedDownloadSize(), [this]
        {
            onCumulativeUploadsUpdated();
        } );
    }

    void synchronizationIsCompleted()
    {
        m_opinionController.approveCumulativeUploads( [this]
        {
            modifyIsCompleted();
        });
    }

    virtual void modifyIsCompleted()
    {
        DBG_MAIN_THREAD

        _ASSERT( m_sandboxRootHash )

        m_drive.m_fsTree = std::move(m_sandboxFsTree);
        m_drive.m_rootHash = *m_sandboxRootHash;
        m_drive.m_fsTreeLtHandle = *m_sandboxFsTreeLtHandle;
        m_sandboxFsTreeLtHandle.reset();

//        m_isSynchronizing = false;

        finishTask();
    }

    void breakTorrentDownloadAndRunNextTask()
    {
        DBG_MAIN_THREAD

        m_drive.m_isWaitingNextTask = false;

        if ( m_drive.m_isRemovingUnusedTorrents )
        {
            // wait the end of the removing
            m_drive.m_isWaitingNextTask = true;
            return;
        }

        m_taskIsStopped = true;
        
        if ( m_downloadingLtHandle )
        {
            if ( auto session = m_drive.m_session.lock(); session )
            {
                //
                // We must break torrent downloading because it could be unavailable
                //
                session->removeDownloadContext( *m_downloadingLtHandle );

                lt_handle torrentHandle = *m_downloadingLtHandle;
                m_downloadingLtHandle.reset();

                session->removeTorrentsFromSession( {torrentHandle}, [this]
                {
                    DBG_MAIN_THREAD
                    _LOG( "breakTorrentDownloadAndm_drive.runNextTask: torrent is removed" );

                    //TODO: move downloaded files from sandbox to drive (for catching-up only)

                    finishTask();
                }, true );
                _LOG( "breakTorrentDownloadAndRunNextTask: remove torrents " )
            }
        } else if ( ! isFinishCallable() )
        {
            // We have already executed all actions for modification
            _LOG( "breakTorrentDownloadAndRunNextTask: m_sandboxCalculated " )
            finishTask();
        } else
        {
            _LOG( "breakTorrentDownloadAndRunNextTask: nothing " )
            // We cannot break torrent download.
            // Therefore, we will wait the end of current task, that will call m_drive.runNextTask()
        }
    }

    // updates drive (1st step after approve)
    // - remove torrents from session
    //
    void startSynchronizingDriveWithSandbox()
    {
        DBG_MAIN_THREAD

        _ASSERT( !m_taskIsStopped )

        auto& torrentHandleMap = m_drive.m_torrentHandleMap;

        // Prepare map for detecting of used files
        for ( auto& it : torrentHandleMap )
        {
            it.second.m_isUsed = false;
        }

        // Mark used files
        markUsedFiles( *m_sandboxFsTree );

        // Prepare set<> for to be removed torrents
        std::set<lt::torrent_handle> toBeRemovedTorrents;

        // Add unused files into set<>
        for ( const auto& it : torrentHandleMap )
        {
            const UseTorrentInfo& info = it.second;
            if ( !info.m_isUsed )
            {
                if ( info.m_ltHandle.is_valid())
                {
                    toBeRemovedTorrents.insert( info.m_ltHandle );
                }
            }
        }

        // Add current fsTree torrent handle
        toBeRemovedTorrents.insert( m_drive.m_fsTreeLtHandle );

        // Remove unused torrents
        if ( auto session = m_drive.m_session.lock(); session )
        {
            _LOG( "toBeRemovedTorrents.size()=" << toBeRemovedTorrents.size() )
            session->removeTorrentsFromSession( toBeRemovedTorrents, [this]
            {
                m_drive.executeOnBackgroundThread( [this]
                {
                    continueSynchronizingDriveWithSandbox();
                });
            }, false);
        }
    }

    void getSandboxDriveSizes( uint64_t& metaFilesSize, uint64_t& driveSize ) const
    {
        DBG_BG_THREAD

        std::error_code err;

        if ( fs::exists( m_drive.m_sandboxRootPath, err ))
        {
            metaFilesSize = fs::file_size( m_drive.m_sandboxFsTreeTorrent );
            driveSize = 0;
            m_sandboxFsTree->getSizes( m_drive.m_driveFolder, m_drive.m_torrentFolder, metaFilesSize,
                                       driveSize );
            driveSize += metaFilesSize;
        } else
        {
            metaFilesSize = 0;
            driveSize = 0;
        }
    }

    uint64_t sandboxFsTreeSize() const
    {
        std::error_code err;

        if ( fs::exists( m_drive.m_sandboxFsTreeFile, err ))
        {
            return fs::file_size( m_drive.m_sandboxFsTreeFile, err );
        }
        return 0;
    }

    ReplicatorList getUploaders()
    {
        auto replicators = m_drive.getAllReplicators();
        replicators.push_back( m_drive.m_driveOwner );
        return replicators;
    }

private:

    void onCumulativeUploadsUpdated()
    {
        DBG_MAIN_THREAD

        if ( m_taskIsStopped )
        {
            finishTask();
            return;
        }

        _ASSERT( m_sandboxRootHash )

        const auto& keyPair = m_drive.m_replicator.keyPair();
        SingleOpinion opinion( keyPair.publicKey().array());

        m_opinionController.fillOpinion( opinion.m_uploadLayout );

        opinion.Sign( keyPair,
                      m_drive.m_driveKey,
                      getModificationTransactionHash(),
                      *m_sandboxRootHash,
                      m_fsTreeSize,
                      m_metaFilesSize,
                      m_sandboxDriveSize );

        m_myOpinion = std::optional<ApprovalTransactionInfo>{{m_drive.m_driveKey.array(),
                                                                     getModificationTransactionHash().array(),
                                                                     m_sandboxRootHash->array(),
                                                                     m_fsTreeSize,
                                                                     m_metaFilesSize,
                                                                     m_sandboxDriveSize,
                                                                     {std::move( opinion )}}};

        m_drive.executeOnBackgroundThread( [this]
                                           {
                                               DBG_BG_THREAD

                                               _ASSERT( m_myOpinion )

                                               saveSingleApprovalTransaction( m_myOpinion );

                                               m_drive.executeOnSessionThread( [this]
                                                                               {
                                                                                   myOpinionIsCreated();
                                                                               } );
                                           } );
    }

    virtual void continueSynchronizingDriveWithSandbox() = 0;

    virtual uint64_t getToBeApprovedDownloadSize() = 0;

    virtual void myOpinionIsCreated() = 0;

    // Whether the finishTask can be called by the task itself
    virtual bool isFinishCallable() = 0;
};

} // namespace sirius::drive

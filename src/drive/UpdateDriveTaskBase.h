/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "DriveTaskBase.h"
#include "DriveParams.h"

namespace sirius::drive
{

namespace fs = std::filesystem;

class UpdateDriveTaskBase : public DriveTaskBase
{

protected:

    ModifyOpinionController& m_opinionController;

    std::optional<ApprovalTransactionInfo> m_myOpinion;

    std::unique_ptr<FsTree> m_sandboxFsTree;
    std::optional<InfoHash> m_sandboxRootHash;
    uint64_t m_metaFilesSize = 0;
    uint64_t m_sandboxDriveSize = 0;
    uint64_t m_fsTreeSize = 0;


    std::optional<lt_handle> m_sandboxFsTreeLtHandle;

    std::optional<lt_handle> m_downloadingLtHandle;
    std::optional<lt_handle> m_fsTreeOrActionListHandle;

    bool m_sandboxCalculated = false;

    bool m_taskIsInterrupted = false;

public:

    void onDriveClose( const DriveClosureRequest& closureRequest ) override
    {
        if ( m_taskIsInterrupted )
        {
            return;
        }

        breakTorrentDownloadAndRunNextTask();
    }

    bool manualSynchronize( const SynchronizationRequest& request ) override
    {
        if ( !m_taskIsInterrupted )
        {
            breakTorrentDownloadAndRunNextTask();
        }

        return true;
    }

protected:

    virtual const Hash256& getModificationTransactionHash() = 0;

    UpdateDriveTaskBase(
            const DriveTaskType& type,
            DriveParams& drive,
            ModifyOpinionController& opinionTaskController)
            : DriveTaskBase( type, drive )
            , m_opinionController( opinionTaskController )
    {}

    void myRootHashIsCalculated()
    {
        DBG_MAIN_THREAD

        _ASSERT( m_sandboxFsTree )
        _ASSERT( m_sandboxRootHash )

        if ( m_taskIsInterrupted )
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
        m_opinionController.updateCumulativeUploads( getModificationTransactionHash(), m_drive.getDonatorShard(), getToBeApprovedDownloadSize(), [this]
        {
            onCumulativeUploadsUpdated();
        } );
    }

    void synchronizationIsCompleted()
    {
        DBG_MAIN_THREAD

        m_opinionController.approveCumulativeUploads( getModificationTransactionHash(), [this]
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
        m_drive.m_lastApprovedModification = getModificationTransactionHash();

        m_drive.updateStreamMap();
//        m_isSynchronizing = false;

        finishTask();
    }

    void breakTorrentDownloadAndRunNextTask()
    {
        DBG_MAIN_THREAD

        m_taskIsInterrupted = true;

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
        }
        else
        {
            tryBreakTask();
        }
//        {
//            _LOG( "breakTorrentDownloadAndRunNextTask: nothing " )
//            // We cannot break torrent download.
//            // Therefore, we will wait the end of current task, that will call m_drive.runNextTask()
//        }
    }

    // updates drive (1st step after approve)
    // - remove torrents from session
    //
    void startSynchronizingDriveWithSandbox()
    {
        DBG_MAIN_THREAD

        _ASSERT( !m_taskIsInterrupted )

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
            _ASSERT( it.first != Hash256() )
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

        // Now, all drive files and torrents are placed directly on drive (not really in sandbox)
        if ( !fs::exists( m_drive.m_driveRootPath, err ))
        {
            metaFilesSize = 0;
            driveSize = 0;
            return;
        }

        metaFilesSize = fs::file_size( m_drive.m_sandboxFsTreeTorrent );
        driveSize = fs::file_size( m_drive.m_sandboxFsTreeFile );

        std::set<InfoHash> files;
        m_sandboxFsTree->getUniqueFiles( files );
        for ( const auto& file: files )
        {
            metaFilesSize += fs::file_size( m_drive.m_torrentFolder / toString(file) );
            driveSize += fs::file_size( m_drive.m_driveFolder / toString(file) );
        }

        driveSize += metaFilesSize;

        _LOG( "Drive Sizes " << m_drive.m_driveKey << " " << driveSize << " " << metaFilesSize )
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

        if ( m_taskIsInterrupted )
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

protected:

    void finishTask() override
    {
        DBG_MAIN_THREAD

        if ( m_fsTreeOrActionListHandle )
        {
            if ( auto session = m_drive.m_session.lock(); session )
            {
                lt_handle torrentHandle = *m_fsTreeOrActionListHandle;
                m_fsTreeOrActionListHandle.reset();

                session->removeTorrentsFromSession( {torrentHandle}, [this]
                {
                    DBG_MAIN_THREAD
                    DriveTaskBase::finishTask();
                }, false );
            }
        } else
        {
            DriveTaskBase::finishTask();
        }
    }

private:

    virtual void continueSynchronizingDriveWithSandbox() = 0;

    virtual uint64_t getToBeApprovedDownloadSize() = 0;

    virtual void myOpinionIsCreated() = 0;

    // Whether the finishTask can be called by the task itself
    virtual void tryBreakTask() = 0;
};

} // namespace sirius::drive

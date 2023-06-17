/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "DriveTaskBase.h"
#include "drive/FlatDrive.h"
#include "drive/FsTree.h"

namespace sirius::drive {

class CancelModificationDriveTask: public DriveTaskBase
{

private:

    const mobj<ModificationCancelRequest> m_request;

    ModifyOpinionController& m_opinionTaskController;

public:

    CancelModificationDriveTask(
            mobj<ModificationCancelRequest>&& request,
            DriveParams& drive,
            ModifyOpinionController& opinionTaskController)
            : DriveTaskBase(DriveTaskType::MODIFICATION_CANCEL, drive )
            , m_request(std::move(request))
            , m_opinionTaskController(opinionTaskController)
    {
        _ASSERT( m_request )
    }

    void run() override
    {
        DBG_MAIN_THREAD

        m_opinionTaskController.disapproveCumulativeUploads( m_request->m_modifyTransactionHash, [this] {
            onCumulativeUploadsDisapproved();
        });
    }

    void terminate() override
    {
        DBG_MAIN_THREAD
    }


private:

    void onCumulativeUploadsDisapproved()
    {
        DBG_MAIN_THREAD

        auto& torrentHandleMap = m_drive.m_torrentHandleMap;

        for( auto& it : torrentHandleMap )
        {
            it.second.m_isUsed = false;
        }

        markUsedFiles( *m_drive.m_fsTree );

        std::set<lt::torrent_handle> toBeRemovedTorrents;

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

        if ( auto session = m_drive.m_session.lock(); session )
        {
            session->removeTorrentsFromSession( toBeRemovedTorrents, [this]
            {
                m_drive.executeOnBackgroundThread( [this]
                {
                    clearDrive();
                });
                }, false);
        }
    }

    void clearDrive()
    {
        DBG_BG_THREAD

        try
        {
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
        }
        catch ( const std::exception& ex )
        {
            _LOG_ERR( "exception during cancel: " << ex.what() );
        }

        m_drive.executeOnSessionThread( [this]
                                        {
                                            cancelModificationIsCompleted();
                                        } );
    }

    void cancelModificationIsCompleted()
    {
        auto * dbgEventHandler = m_drive.m_dbgEventHandler;
        if ( dbgEventHandler )
        {
            dbgEventHandler->driveModificationIsCanceled( m_drive.m_replicator, m_drive.m_replicator.dbgReplicatorKey(), m_request->m_modifyTransactionHash );
        }
        finishTask();
    }
};

std::unique_ptr<DriveTaskBase> createModificationCancelTask( mobj<ModificationCancelRequest>&& request,
                                                             DriveParams& drive,
                                                             ModifyOpinionController& opinionTaskController  )
{
    return std::make_unique<CancelModificationDriveTask>( std::move(request), drive, opinionTaskController  );
}

}

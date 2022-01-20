/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "drive/DownloadLimiter.h"
#include "drive/DriveTask.h"
#include "drive/FlatDrive.h"
#include "drive/FlatDrivePaths.h"
#include "drive/FsTree.h"
#include "drive/ActionList.h"

namespace sirius::drive {

class CancelModificationDriveTask: public sirius::drive::BaseDriveTask
{

private:

    ModificationCancelRequest m_request;
    OpinionTaskController& m_opinionTaskController;

public:

    CancelModificationDriveTask(
            const ModificationCancelRequest& request,
            const DriveTaskType& type,
            TaskController& drive,
            OpinionTaskController& opinionTaskController,
            const FlatDrivePaths& flatDrivePaths)
            : BaseDriveTask(type, drive, flatDrivePaths)
            , m_request(request)
            , m_opinionTaskController(opinionTaskController)
    {}

    void run() override
    {
        DBG_MAIN_THREAD

        m_opinionTaskController.downgradeCumulativeUploads([this] {
            onCumulativeUploadsDowngraded();
        });
    }

private:

    void onCumulativeUploadsDowngraded()
    {
        DBG_MAIN_THREAD

        auto& torrentHandleMap = m_drive.getTorrentHandleMap();

        for( auto& it : torrentHandleMap )
        {
            it.second.m_isUsed = false;
        }

        markUsedFiles( *m_drive.getFsTree() );

        std::set<InfoHash> filesToRemove;

        for ( auto it = torrentHandleMap.begin(); it != torrentHandleMap.end(); ) {
            if ( it->second.m_isUsed )
            {
                filesToRemove.insert( it->first );
                it = torrentHandleMap.erase( it );
            }
            else
            {
                it++;
            }
        }

        m_drive.enqueueBackgroundThreadTask( [=, this]
        {
            clearDrive(filesToRemove);
        });
    }

    void clearDrive( const std::set<InfoHash>& filesToRemove )
    {
        DBG_BG_THREAD

        try
        {
            // remove unused files and torrent files from the drive
            for( const auto& hash : filesToRemove )
            {
                std::string filename = hashToFileName( hash );
                fs::remove( fs::path( m_flatDrivePaths.m_driveFolder ) / filename );
                fs::remove( fs::path( m_flatDrivePaths.m_torrentFolder ) / filename );
            }
        }
        catch ( const std::exception& ex )
        {
            _LOG_ERR( "exception during cancel: " << ex.what() );
        }

        m_drive.enqueueMainThreadTask( [this] {
            finishCancelModification();
        });
    }

    void finishCancelModification()
    {
        auto * dbgEventHandler = m_drive.getDbgReplicatorEventHandler();
        if ( dbgEventHandler )
        {
            dbgEventHandler->driveModificationIsCanceled( m_replicator, m_drive.getKeyPair().publicKey(), *m_modificationCanceledTx );
        }
        runNextTask();
    }
};

}
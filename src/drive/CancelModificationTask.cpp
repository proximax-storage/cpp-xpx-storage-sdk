/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "DriveTask.h"
#include "drive/FlatDrive.h"
#include "drive/FsTree.h"

namespace sirius::drive {

class CancelModificationDriveTask: public sirius::drive::BaseDriveTask
{

private:

    const mobj<ModificationCancelRequest> m_request;

    ModifyOpinionController& m_opinionTaskController;

public:

    CancelModificationDriveTask(
            mobj<ModificationCancelRequest>&& request,
            TaskContext& drive,
            ModifyOpinionController& opinionTaskController)
            : BaseDriveTask(DriveTaskType::MODIFICATION_CANCEL, drive )
            , m_request(request)
            , m_opinionTaskController(opinionTaskController)
    {
        _ASSERT( m_request )
    }

    void run() override
    {
        DBG_MAIN_THREAD

        m_opinionTaskController.disapproveCumulativeUploads([this] {
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

        std::set<InfoHash> filesToRemove;

        for ( auto it = torrentHandleMap.begin(); it != torrentHandleMap.end(); ) {
            if ( !it->second.m_isUsed )
            {
                filesToRemove.insert( it->first );
                it = torrentHandleMap.erase( it );
            }
            else
            {
                it++;
            }
        }

        m_drive.executeOnBackgroundThread( [=, this]
                                           {
                                               clearDrive( filesToRemove );
                                           } );
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
                fs::remove( fs::path( m_drive.m_driveFolder ) / filename );
                fs::remove( fs::path( m_drive.m_torrentFolder ) / filename );
            }
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
            dbgEventHandler->driveModificationIsCanceled( m_drive.m_replicator, m_drive.m_replicator.keyPair().publicKey(), m_request->m_modifyTransactionHash );
        }
        finishTask();
    }
};

std::unique_ptr<BaseDriveTask> createModificationCancelTask( mobj<ModificationCancelRequest>&& request,
                                                             TaskContext& drive,
                                                             ModifyOpinionController& opinionTaskController  )
{
    return std::make_unique<CancelModificationDriveTask>( std::move(request), drive, opinionTaskController  );
}

}
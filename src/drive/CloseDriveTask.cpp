/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "drive/DownloadLimiter.h"
#include "DriveTask.h"
#include "drive/FlatDrive.h"
#include "drive/FlatDrivePaths.h"
#include "drive/FsTree.h"

namespace sirius::drive
{
class CloseDriveDriveTask : public BaseDriveTask
{

private:

    const mobj<DriveClosureRequest> m_request;

public:

    CloseDriveDriveTask( mobj<DriveClosureRequest>&& request,
                         TaskContext& drive ) :
            BaseDriveTask(DriveTaskType::DRIVE_CLOSURE, drive),
            m_request(request)
    {
        _ASSERT( m_request )
    }

    void run() override
    {
        DBG_MAIN_THREAD

        //
        // Remove torrents from session
        //

        std::set<lt_handle> tobeRemovedTorrents;

        for( auto& [key,value]: m_drive.m_torrentHandleMap )
        {
            tobeRemovedTorrents.insert( value.m_ltHandle );
        }

        tobeRemovedTorrents.insert( m_drive.m_fsTreeLtHandle );

        if ( auto session = m_drive.m_session.lock(); session )
        {
            session->removeTorrentsFromSession( tobeRemovedTorrents, [this](){
                m_drive.m_replicator.closeDriveChannels( m_request->m_removeDriveTx, m_drive.m_driveKey );
            });
        }

        m_drive.executeOnBackgroundThread( [this]
                                           {
                                               removeAllDriveData();
                                           } );
    }

    void terminate() override
    {
        DBG_MAIN_THREAD
    }

    bool shouldCatchUp( const PublishedModificationApprovalTransactionInfo& transaction ) override
    {
        DBG_MAIN_THREAD

        _ASSERT(0);
        return false;
    }

    bool shouldCancelModify( const ModificationCancelRequest& cancelRequest ) override
    {
        DBG_MAIN_THREAD

        _ASSERT(0)
    }

private:

    void removeAllDriveData()
    {
        DBG_BG_THREAD

        try {
            // remove drive root folder and sandbox
            fs::remove_all( m_drive.m_sandboxRootPath );
            fs::remove_all( m_drive.m_driveRootPath );
        }
        catch ( const std::exception& ex )
        {
            _LOG_ERR( "exception during removeAllDriveData: " << ex.what() );
            finishTask();
        }

        if ( auto * dbgEventHandler = m_drive.m_dbgEventHandler; dbgEventHandler )
        {
            dbgEventHandler->driveIsClosed( m_drive.m_replicator, m_drive.m_driveKey, m_request->m_removeDriveTx );
        }

        if ( auto session = m_drive.m_session.lock(); session )
        {
            boost::asio::post(session->lt_session().get_context(), [this] {
                m_drive.m_replicator.finishDriveClosure( m_drive.m_driveKey );
            });
        }
    }
};

std::unique_ptr<BaseDriveTask> createDriveClosureTask( mobj<DriveClosureRequest>&& request,
                                                       TaskContext& drive )
{
    return std::make_unique<CloseDriveDriveTask>( std::move(request), drive );
}


}
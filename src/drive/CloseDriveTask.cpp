/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "DownloadLimiter.h"
#include "DriveTaskBase.h"
#include "drive/FlatDrive.h"

#include "drive/FsTree.h"

namespace sirius::drive
{
class CloseDriveDriveTask : public DriveTaskBase
{

private:

    const mobj<DriveClosureRequest> m_request;

public:

    CloseDriveDriveTask( mobj<DriveClosureRequest>&& request,
                         DriveParams& drive ) :
            DriveTaskBase(DriveTaskType::DRIVE_CLOSURE, drive),
            m_request( std::move(request) )
    {
        _ASSERT( m_request )
    }

    void run() override
    {
        DBG_MAIN_THREAD

		_LOG( "Started Drive Closure " << m_drive.m_driveKey )

        //
        // Remove torrents from session
        //

        std::set<lt_handle> tobeRemovedTorrents;

        for( auto& [key,value]: m_drive.m_torrentHandleMap )
        {
            _ASSERT( key != Hash256() )
            tobeRemovedTorrents.insert( value.m_ltHandle );
        }

        tobeRemovedTorrents.insert( m_drive.m_fsTreeLtHandle );

        if ( auto session = m_drive.m_session.lock(); session )
        {
            session->removeTorrentsFromSession( tobeRemovedTorrents, [this]()
            {
                if ( m_request->m_removeDriveTx )
                {
                    m_drive.m_replicator.closeDriveChannels( std::make_unique<Hash256>(*m_request->m_removeDriveTx), m_drive.m_driveKey);
                }
                m_drive.executeOnBackgroundThread( [this]
                                                   {
                                                       removeAllDriveData();
                                                   } );
            }, false);
        }
    }

    void terminate() override
    {
        DBG_MAIN_THREAD
    }

    bool onApprovalTxPublished( const PublishedModificationApprovalTransactionInfo& transaction ) override
    {
        DBG_MAIN_THREAD

        _ASSERT(0);
        return false;
    }

    bool shouldCancelModify( const ModificationCancelRequest& cancelRequest ) override
    {
        DBG_MAIN_THREAD

        _ASSERT(0)
        return false;
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

        if ( auto session = m_drive.m_session.lock(); session )
        {
            boost::asio::post(session->lt_session().get_context(), [this] {
                if ( auto * dbgEventHandler = m_drive.m_dbgEventHandler; dbgEventHandler)
                {
                	Hash256 closeId = m_request->m_removeDriveTx ? *m_request->m_removeDriveTx : Hash256();
                    dbgEventHandler->driveIsClosed( m_drive.m_replicator, m_drive.m_driveKey, closeId );
                }

                m_drive.m_replicator.finishDriveClosure( m_drive.m_driveKey );
            });
        }
    }
};

std::unique_ptr<DriveTaskBase> createDriveClosureTask( mobj<DriveClosureRequest>&& request,
                                                       DriveParams& drive )
{
    return std::make_unique<CloseDriveDriveTask>( std::move(request), drive );
}


}

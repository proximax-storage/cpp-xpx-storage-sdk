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

namespace sirius::drive
{
class CloseDriveDriveTask : public BaseDriveTask
{

private:

    DriveClosureRequest m_request;

public:
    void run() override
    {
        DBG_MAIN_THREAD

        //
        // Remove torrents from session
        //

        std::set<lt_handle> tobeRemovedTorrents;

        for( auto& [key,value]: m_drive.getTorrentHandleMap() )
        {
            tobeRemovedTorrents.insert( value.m_ltHandle );
        }

        tobeRemovedTorrents.insert( m_drive.getFsTreeHandle() );

        if ( auto session = m_drive.getSession().lock(); session )
        {
            session->removeTorrentsFromSession( tobeRemovedTorrents, [this](){
                m_drive.getReplicator().closeDriveChannels( m_request.m_removeDriveTx, m_drive.getDriveKey() );
            });
        }
    }

    void terminate() override
    {
        DBG_MAIN_THREAD
    }

    bool shouldCatchUp( const PublishedModificationApprovalTransactionInfo& transaction ) override
    {
        _ASSERT(0);
        return false;
    }

    bool processedOpinion( const ApprovalTransactionInfo& anOpinion ) override
    {
        return false;
    }
};
}
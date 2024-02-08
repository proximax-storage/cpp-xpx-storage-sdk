/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "SynchronizationTaskBase.h"

namespace sirius::drive
{

namespace fs = std::filesystem;

class CatchingUpTask : public SyncTaskBase
{
private:

    mobj<CatchingUpRequest>      m_request;

public:

    CatchingUpTask( mobj<CatchingUpRequest>&& request,
                    DriveParams& drive,
                    ModifyOpinionController& opinionTaskController )
                    : SyncTaskBase(DriveTaskType::CATCHING_UP, drive, opinionTaskController)
                    , m_request( std::move(request) )
    {
        SIRIUS_ASSERT( m_request )
    }

    void interruptTask( const ModificationCancelRequest& cancelRequest, bool& cancelRequestIsAccepted ) override {

        DBG_MAIN_THREAD

        // It is a very rare situation:
        // After the initialization we are running the task
        // Without actually need to catch up
        // During the task is beeing finished the cancel is requested
        if ( cancelRequest.m_modifyTransactionHash == m_opinionController.notApprovedModificationId() )
        {
            SIRIUS_ASSERT( cancelRequest.m_modifyTransactionHash != m_request->m_modifyTransactionHash )
            SIRIUS_ASSERT( m_drive.m_lastApprovedModification == m_request->m_modifyTransactionHash )
            cancelRequestIsAccepted = true;
            return;
        }

        cancelRequestIsAccepted = false;
    }

    // Returns 'true' if 'CatchingUp' should be started
    bool onApprovalTxPublished( const PublishedModificationApprovalTransactionInfo& transaction ) override
    {
        DBG_MAIN_THREAD

        if ( m_taskIsInterrupted )
        {
            return true;
        }

        interruptTorrentDownloadAndRunNextTask();
        return true;
    }

    const Hash256& getModificationTransactionHash() override
    {
        return m_request->m_modifyTransactionHash;
    }

    void myOpinionIsCreated() override
    {
        DBG_MAIN_THREAD

        SIRIUS_ASSERT( m_myOpinion )

        sendSingleApprovalTransaction( *m_myOpinion );

        SyncTaskBase::myOpinionIsCreated();
    }

protected:

    const Hash256& getRootHash() override
    {
        return m_request->m_rootHash;
    }

public:

    uint64_t getToBeApprovedDownloadSize() override
    {
        return 0;
    }
};

std::unique_ptr<DriveTaskBase> createCatchingUpTask( mobj<CatchingUpRequest>&& request,
                                                     DriveParams& drive,
                                                     ModifyOpinionController& opinionTaskController )
{
    return std::make_unique<CatchingUpTask>( std::move(request), drive, opinionTaskController);
}

}

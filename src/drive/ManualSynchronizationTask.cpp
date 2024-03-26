/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/


#include "SynchronizationTaskBase.h"

namespace sirius::drive
{

class ManualSyncTask
        : public SyncTaskBase
{
private:

    mobj<SynchronizationRequest> m_request;
    bool m_syncCompletedSuccessfully = false;

public:

    ManualSyncTask( mobj<SynchronizationRequest>&& request,
                               DriveParams& drive,
                               ModifyOpinionController& opinionTaskController )
            : SyncTaskBase( DriveTaskType::MANUAL_SYNCHRONIZATION, drive, opinionTaskController )
            , m_request( std::move( request ))
    {
        SIRIUS_ASSERT( m_request )
    }

    void onCancelModifyTx( const ModificationCancelRequest& cancelRequest, bool& cancelRequestIsAccepted ) override
    {
        DBG_MAIN_THREAD

        SIRIUS_ASSERT(0)
        cancelRequestIsAccepted = false;
    }

    // Returns 'true' if 'CatchingUp' should be started
    bool onApprovalTxPublished( const PublishedModificationApprovalTransactionInfo& transaction ) override
    {
        DBG_MAIN_THREAD

        // TODO seems it should never be the case
        SIRIUS_ASSERT( 0 );

        return false;
    }

    void onModificationInitiated( const ModificationRequest& request ) override
    {
        DBG_MAIN_THREAD

        // TODO seems it should never be the case
        SIRIUS_ASSERT( 0 );
    }

    const Hash256& getModificationTransactionHash() override
    {
        return m_request->m_modificationIdentifier;
    }

protected:

    const Hash256& getRootHash() override
    {
        return m_request->m_rootHash;
    }

public:

    void modificationCompletedSuccessfully() override
    {
        m_syncCompletedSuccessfully = true;
        SyncTaskBase::modificationCompletedSuccessfully();
    }

    uint64_t getToBeApprovedDownloadSize() override
    {
        return 0;
    }

protected:

	void onDriveChangedAfterApproving() override
	{
		DBG_MAIN_THREAD

		// Ignore cumulative uploads approval, skip right to modificationCompletedSuccessfully().
		modificationCompletedSuccessfully();
	}

    void removeTorrentsAndFinishTask() override
    {
        m_request->m_callback( SynchronizationResponse{m_syncCompletedSuccessfully} );
        sirius::drive::UpdateDriveTaskBase::removeTorrentsAndFinishTask();
    }

private:

	void updateOpinionUploads() override {
        myOpinionIsCreated();
	};
};

std::unique_ptr<DriveTaskBase> createManualSynchronizationTask( mobj <SynchronizationRequest>&& request,
                                                                DriveParams& drive,
                                                                ModifyOpinionController& opinionTaskController )
{
    return std::make_unique<ManualSyncTask>( std::move( request ), drive, opinionTaskController );
}



}

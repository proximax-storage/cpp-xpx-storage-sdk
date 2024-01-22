/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/


#include "SynchronizationTaskBase.h"

namespace sirius::drive
{

class ManualSynchronizationTask
        : public SynchronizationTaskBase
{
private:

    mobj<SynchronizationRequest> m_request;
    bool m_taskIsExecuted = false;

public:

    ManualSynchronizationTask( mobj<SynchronizationRequest>&& request,
                               DriveParams& drive,
                               ModifyOpinionController& opinionTaskController )
            : SynchronizationTaskBase( DriveTaskType::MANUAL_SYNCHRONIZATION, drive, opinionTaskController )
            , m_request( std::move( request ))
    {
        SIRIUS_ASSERT( m_request )
    }

    bool shouldCancelModify( const ModificationCancelRequest& cancelRequest ) override
    {

        DBG_MAIN_THREAD

        SIRIUS_ASSERT( 0 )

        return false;
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

    void modifyIsCompleted() override
    {
        m_taskIsExecuted = true;
        SynchronizationTaskBase::modifyIsCompleted();
    }

    uint64_t getToBeApprovedDownloadSize() override
    {
        return 0;
    }

protected:

	void synchronizationIsCompleted() override
	{
		DBG_MAIN_THREAD

		// Ignore cumulative uploads approval, skip right to modifyIsCompleted().
		modifyIsCompleted();
	}

    void finishTask() override
    {
        m_request->m_callback( SynchronizationResponse{m_taskIsExecuted} );
        sirius::drive::UpdateDriveTaskBase::finishTask();
    }

private:

	void prepareForSandboxSynchronization() override {
		_LOG( "Preparing for sandbox synchronization from Manual Synchronization task." )
		_LOG( "Skipping to myOpinionIsCreated()" )
		m_drive.executeOnBackgroundThread( [this]
										   {
											 DBG_BG_THREAD

											 m_drive.executeOnSessionThread( [this]
																			 {
																			   myOpinionIsCreated();
																			 } );
										   } );
	};
};

std::unique_ptr<DriveTaskBase> createManualSynchronizationTask( mobj <SynchronizationRequest>&& request,
                                                                DriveParams& drive,
                                                                ModifyOpinionController& opinionTaskController )
{
    return std::make_unique<ManualSynchronizationTask>( std::move( request ), drive, opinionTaskController );
}



}

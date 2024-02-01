
class UpdateDriveTaskBase
{
    std::optional<lt_handle>                m_downloadingLtHandle;
    std::optional<ApprovalTransactionInfo>  m_myOpinion;
    
    ModificationStatus      m_modificationStatus = ModificationStatus::SUCCESS;
    std::unique_ptr<FsTree> m_sandboxFsTree;
    std::optional<InfoHash> m_sandboxRootHash;
    
    std::optional<lt_handle> m_sandboxFsTreeLtHandle;
    
    std::optional<lt_handle> m_downloadingLtHandle;
    std::optional<lt_handle> m_fsTreeOrActionListHandle;
}

class ModifyTaskBase : public UpdateDriveTaskBase
{
    std::map<std::array<uint8_t,32>,ApprovalTransactionInfo> m_receivedOpinions;
    
    bool m_modifyApproveTransactionSent = false;
    bool m_modifyApproveTxReceived = false;
    
    uint64_t m_uploadedDataSize = 0;
}

ModifyDriveTask : ModifyTaskBase
{
    m_actionListIsReceived = false;
}

//---Modify--------------------------------------------------------------------------------------

ModifyDriveTask::run()
{
    m_actionListIsReceived ?= true; // if alredy downloaded

    m_downloadingLtHandle = download(...[]
    {
        m_downloadingLtHandle.reset();
        m_actionListIsReceived = true;
        ...
    })
}

ModifyDriveTask::prepareDownloadMissingFiles()
{
    ? modifyIsCompletedWithError( "modify drive: invalid 'ActionList'", ModificationStatus::INVALID_ACTION_LIST );
      --> m_modificationStatus = ModificationStatus::INVALID_ACTION_LIST;
}

ModifyDriveTask::downloadMissingFiles()
{
    m_downloadingLtHandle = download(...)
    
    m_downloadingLtHandle = download(...[]
    {
        m_downloadingLtHandle.reset();
        ...
    })
}

ModifyDriveTask::modifyFsTreeInSandbox()
{
    DBG_BG_THREAD
    
    m_sandboxFsTree = ...
    m_sandboxRootHash = ...

}

UpdateDriveTaskBase::myRootHashIsCalculated()
{
    m_taskIsInterrupted --> removeTorrentsAndFinishTask()
}

UpdateDriveTaskBase::onCumulativeUploadsUpdated()
{
    m_taskIsInterrupted --> removeTorrentsAndFinishTask()
    
    m_myOpinion = {...}
}

ModifyTaskBase::myOpinionIsCreated()
{
    m_taskIsInterrupted --> removeTorrentsAndFinishTask()
    
    m_sandboxCalculated = true;
    
    if ( m_modifyApproveTxReceived )
    {
        sendSingleApprovalTransaction( *m_myOpinion );
        startSynchronizingDriveWithSandbox();
    }
    else
    {
        shareMyOpinion();
        sendModifyApproveTxWithDelay();
    }
}

//---Finish--------------------------------------------------------------------------------------

UpdateDriveTaskBase::removeTorrentsAndFinishTask()
{
    // remove torrent of fsTreeOrActionList
    
    finishTaskAndRunNext()
}

DriveTaskBase::finishTaskAndRunNext()
{
    "??? REMOVE ACTION_LIST TORRENT ???"
    // clear sandbox_folder
    
    drive->runNextTask()
}

Drive::runNextTask()
{
    if ( m_modificationCancelRequest )
    {
        runCancelModificationTask();
        return;
    }

    if ( m_catchingUpRequest )
    {
        runCatchingUpTask();
        return;
    }

    if ( ! m_deferredModificationRequests.empty())
    {
        runDeferredModificationTask() --> ModifyDriveTask::run()
    }
}

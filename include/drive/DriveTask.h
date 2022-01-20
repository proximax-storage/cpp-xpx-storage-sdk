/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include "FlatDrive.h"
#include "FlatDrivePaths.h"
#include "FsTree.h"

#define DBG_MAIN_THREAD { assert( m_dbgThreadId == std::this_thread::get_id() ); }
#define DBG_BG_THREAD { assert( m_dbgThreadId != std::this_thread::get_id() ); }

namespace sirius::drive
{

class TaskController
{

public:

    virtual ~TaskController() = default;

    virtual const crypto::KeyPair& getKeyPair() = 0;

    virtual const Key& getDriveKey() = 0;

    virtual Key getClient() const = 0;

    virtual std::vector<Key> getReplicators() = 0;

    virtual std::vector<Key> getUploaderKeys() = 0;

    virtual std::weak_ptr<Session>  getSession() = 0;

    virtual mobj<FsTree>&           getFsTree();

    virtual InfoHash&               getRootHash();

    virtual std::map<InfoHash, UseTorrentInfo>& getTorrentHandleMap() = 0;

    virtual lt::torrent_handle&                 getFsTreeHandle() = 0;

    virtual void enqueueMainThreadTask( const std::function<void()>& task ) = 0;

    virtual void enqueueBackgroundThreadTask( const std::function<void()>& task ) = 0;

    virtual void runNextTask() = 0;

    virtual std::thread::id getDbgMainThreadId() = 0;

    virtual std::thread::id getDbgBackgroundThreadId() = 0;

    virtual ReplicatorEventHandler* getReplicatorEventHandler() = 0;

    virtual DbgReplicatorEventHandler* getDbgReplicatorEventHandler() = 0;

    virtual Replicator& getReplicator() = 0;

    virtual std::string getDbgOurPeerName() = 0;
};

class ModifyOpinionController
{
public:

    virtual ~ModifyOpinionController() = default;

    virtual std::optional<Hash256>& getOpinionTrafficIdentifier() = 0;

    virtual void downgradeCumulativeUploads( const std::function<void()>& callback ) = 0;

    virtual void increaseExpectedCumulativeDownload( uint64_t ) = 0;

    virtual std::map<std::array<uint8_t, 32>, ApprovalTransactionInfo>& getOtherOpinions( const Hash256& modification ) = 0;

    virtual void createMyOpinion( const Hash256& sandboxRootHash,
                                  const uint64_t& sandboxDriveSize,
                                  const uint64_t& sandboxFsTreeSize,
                                  const uint64_t& sandboxMetafilesSize,
                                  const std::function<void( const ApprovalTransactionInfo& )>& callback ) = 0;
};

enum class DriveTaskType
{
    DRIVE_CLOSURE,
    MODIFICATION_CANCEL,
    CATCHING_UP,
    MODIFICATION_REQUEST
};

enum class NotificationProcessResult
{
    // The notification has been taken into account and the task is able to continue work properly
    SUCCESS,
    // The notification has been taken into account but the task can not continue work properly
    FAILURE,
    // The notification does not affect the task
    IGNORE,
};

class BaseDriveTask
{

private:

    DriveTaskType m_type;

protected:

    TaskController& m_drive;
    const FlatDrivePaths& m_flatDrivePaths;

    std::thread::id m_dbgThreadId;
    std::string m_dbgOurPeerName;

public:

    explicit BaseDriveTask(
            const DriveTaskType& type,
            TaskController& drive,
            const FlatDrivePaths& flatDrivePaths)
            : m_type(type)
            , m_drive(drive)
            , m_flatDrivePaths(flatDrivePaths)
            , m_dbgThreadId(std::this_thread::get_id())
            , m_dbgOurPeerName(drive.getDbgOurPeerName())
    {}

    virtual ~BaseDriveTask() = default;

    virtual void run() = 0;

    virtual void terminate() = 0;

    DriveTaskType getTaskType()
    {
        return m_type;
    }

    virtual bool shouldCatchUp(
            const PublishedModificationApprovalTransactionInfo& transaction ) = 0;

    virtual bool processedOpinion (
            const ApprovalTransactionInfo& anOpinion ) = 0;

protected:

    // Recursively marks 'm_toBeRemoved' as false
    //
    void markUsedFiles( const Folder& folder )
    {
        DBG_MAIN_THREAD

        for( const auto& child : folder.m_childs )
        {
            if ( isFolder(child) )
            {
                markUsedFiles( getFolder(child) );
            }
            else
            {
                auto& hash = getFile(child).m_hash;

                if ( const auto& it = m_drive.getTorrentHandleMap().find(hash); it != m_drive.getTorrentHandleMap().end() )
                {
                    it->second.m_isUsed = true;
                }
                else
                {
                    LOG( "markUsedFiles: internal error");
                }
            }
        }
    }

};

struct ModificationRequest
{
    InfoHash m_clientDataInfoHash;
    Hash256 m_transactionHash;
    uint64_t m_maxDataSize;
    ReplicatorList m_replicators;
    Key m_clientPublicKey;

    bool m_isCanceled = false;
};

struct CatchingUpRequest
{
    InfoHash m_rootHash;
    Hash256 m_modifyTransactionHash;
};

struct ModificationCancelRequest
{

};

struct DriveClosureRequest
{
    Hash256 m_removeDriveTx;
};

std::shared_ptr<BaseDriveTask> createModificationTask(const ModificationRequest& request);
std::shared_ptr<BaseDriveTask> createCatchingUpTask(const CatchingUpRequest& request);
std::shared_ptr<BaseDriveTask> createModificationCancelTask(const ModificationCancelRequest& request);
std::shared_ptr<BaseDriveTask> createDriveClosureTask(const DriveClosureRequest& request);

}
/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include "drive/FlatDrive.h"
#include "drive/FsTree.h"
#include "DriveParams.h"
#include "ModifyOpinionController.h"
#include "drive/ManualModificationsRequests.h"
#include "drive/Streaming.h"

#undef DBG_MAIN_THREAD
#define DBG_MAIN_THREAD _FUNC_ENTRY; assert( m_dbgThreadId == std::this_thread::get_id() );
#define DBG_BG_THREAD _FUNC_ENTRY; assert( m_dbgThreadId != std::this_thread::get_id() );

namespace sirius::drive
{

class DriveTaskBase
{

private:

    const DriveTaskType m_type;

protected:

    DriveParams& m_drive;

    std::thread::id m_dbgThreadId;
    std::string m_dbgOurPeerName;

public:

    DriveTaskBase(
            const DriveTaskType& type,
            DriveParams& drive )
            : m_type( type ), m_drive( drive )
            , m_dbgThreadId( std::this_thread::get_id())
            , m_dbgOurPeerName( m_drive.m_dbgOurPeerName )
    {
        _LOG( "DriveTaskBase: " << int( type ))
    }

    virtual ~DriveTaskBase() = default;

    virtual void run() = 0;

    virtual void shutdown() = 0;

    virtual void terminateVerification() {};

    DriveTaskType getTaskType()
    {
        DBG_MAIN_THREAD

        return m_type;
    }

    // Returns 'true' if 'CatchingUp' should start
    virtual bool onApprovalTxPublished( const PublishedModificationApprovalTransactionInfo& transaction ) = 0;

    virtual void onApprovalTxFailed( const Hash256& transactionHash )
    {
        DBG_MAIN_THREAD
    }

    virtual void onCancelModifyTx( const ModificationCancelRequest& cancelRequest, bool& cancelRequestIsAccepted )
    {
        DBG_MAIN_THREAD

        cancelRequestIsAccepted = false;
    }


    virtual void onDriveClose( const DriveClosureRequest& closureRequest )
    {
        DBG_MAIN_THREAD
    }

    virtual void onModificationInitiated( const ModificationRequest& request ) {
        DBG_MAIN_THREAD
    }

    virtual void onStreamStarted( const StreamRequest& request ) {
        DBG_MAIN_THREAD
    }

    virtual void onManualModificationInitiated( const InitiateModificationsRequest& request)
    {
        DBG_MAIN_THREAD
    }

    virtual bool processedModifyOpinion( const ApprovalTransactionInfo& anOpinion )
    {
        DBG_MAIN_THREAD

        return false;
    }

    virtual bool processedVerificationOpinion( const VerifyApprovalTxInfo& transactionInfo )
    {
        DBG_MAIN_THREAD

        return false;
    }

    virtual bool processedVerificationCode( const VerificationCodeInfo& info )
    {
        DBG_MAIN_THREAD

        return false;
    }

    virtual void cancelVerification()
    {
        DBG_MAIN_THREAD
    }

    virtual void acceptChunkInfoMessage( ChunkInfo&, const boost::asio::ip::udp::endpoint& streamer )
    {
        // it must be overriden by StreamTask
    }

    virtual void acceptFinishStreamTx( mobj<StreamFinishRequest>&&, std::map<std::array<uint8_t, 32>, ApprovalTransactionInfo>&& )
    {
        // it must be overriden by StreamTask
    }

    // Request from viewer
    virtual std::string acceptGetChunksInfoMessage( const std::array<uint8_t, 32>& streamId,
                                                    uint32_t chunkIndex,
                                                    const boost::asio::ip::udp::endpoint& viewer )
    {
        // it must be overriden by StreamTask

        bool streamFinished = m_drive.m_streamMap.find( Hash256( streamId )) != m_drive.m_streamMap.end();
        std::ostringstream os( std::ios::binary );
        cereal::PortableBinaryOutputArchive archive( os );
        int32_t streamIsEnded = streamFinished ? 0xffffFFFF : 0xffffFFF0;
        archive( streamIsEnded );

        return os.str();
    }
    
    virtual std::optional<std::array<uint8_t,32>> getStreamId()
    {
        return {};
    }

    virtual bool initiateSandboxModifications( const InitiateSandboxModificationsRequest& request )
    {
        DBG_MAIN_THREAD

        return false;
    }

    virtual bool openFile( const OpenFileRequest& request )
    {
        DBG_MAIN_THREAD

        return false;
    }

    virtual bool writeFile( const WriteFileRequest& request )
    {
        DBG_MAIN_THREAD

        return false;
    }

    virtual bool readFile( const ReadFileRequest& request )
    {
        DBG_MAIN_THREAD

        return false;
    }

    virtual bool flush( const FlushRequest& request )
    {
        DBG_MAIN_THREAD

        return false;
    }

    virtual bool closeFile( const CloseFileRequest& request )
    {
        DBG_MAIN_THREAD

        return false;
    }

    virtual bool removeFsTreeEntry( const RemoveFilesystemEntryRequest& request )
    {
        DBG_MAIN_THREAD

        return false;
    }

    virtual bool pathExist( const PathExistRequest& request )
    {
        DBG_MAIN_THREAD

        return false;
    }

    virtual bool pathIsFile( const PathIsFileRequest& request )
    {
        DBG_MAIN_THREAD

        return false;
    }

    virtual bool fileSize( const FileSizeRequest& request )
    {
        DBG_MAIN_THREAD

        return false;
    }

    virtual bool createDirectories( const CreateDirectoriesRequest& request )
    {
        DBG_MAIN_THREAD

        return false;
    }

    virtual bool folderIteratorCreate( const FolderIteratorCreateRequest& request )
    {
        DBG_MAIN_THREAD

        return false;
    }

    virtual bool folderIteratorDestroy( const FolderIteratorDestroyRequest& request )
    {
        DBG_MAIN_THREAD

        return false;
    }

    virtual bool folderIteratorHasNext( const FolderIteratorHasNextRequest& request )
    {
        DBG_MAIN_THREAD

        return false;
    }

    virtual bool folderIteratorNext( const FolderIteratorNextRequest& request )
    {
        DBG_MAIN_THREAD

        return false;
    }

    virtual bool moveFsTreeEntry( const MoveFilesystemEntryRequest& )
    {
        DBG_MAIN_THREAD

        return false;
    }

    virtual bool applySandboxModifications( const ApplySandboxModificationsRequest& request )
    {
        DBG_MAIN_THREAD

        return false;
    }

    virtual bool evaluateStorageHash( const EvaluateStorageHashRequest& request )
    {
        DBG_MAIN_THREAD

        return false;
    }

    virtual bool applyStorageModifications( const ApplyStorageModificationsRequest& request )
    {
        DBG_MAIN_THREAD

        return false;
    }

    virtual bool manualSynchronize( const SynchronizationRequest& request )
    {
        DBG_MAIN_THREAD

        return false;
    }
    
    virtual void tryConnectPeer( const Hash256& tx, const boost::asio::ip::udp::endpoint& endpoint )
    {
        DBG_MAIN_THREAD
    }

protected:


    virtual void finishTaskAndRunNext()
    {
        DBG_MAIN_THREAD

        m_drive.executeOnBackgroundThread( [this]
        {
           DBG_BG_THREAD

           std::error_code err;

           if ( !fs::exists( m_drive.m_sandboxRootPath, err ))
           {
               fs::create_directories( m_drive.m_sandboxRootPath );
               fs::create_directories( m_drive.m_sandboxStreamTFolder );
           }
           else
           {
               for( const auto& entry: std::filesystem::directory_iterator( m_drive.m_sandboxRootPath ))
               {
                   fs::remove_all( entry.path(), err );
                   _LOG( "fs::remove_all" );
                   if ( err )
                   {
                       _LOG_WARN( "remove sandbox error: " << err )
                   }
               }
           }

           m_drive.executeOnSessionThread( [this]
                                           {
                                               m_drive.runNextTask();
                                           } );
        } );
    }

    void markUsedFiles( const Folder& folder )
    {
        DBG_MAIN_THREAD

        for ( const auto&[name, child] : folder.childs())
        {
            if ( isFolder( child ))
            {
                markUsedFiles( getFolder( child ));
            } else
            {
                auto& hash = getFile( child ).hash();

                if ( const auto& it = m_drive.m_torrentHandleMap.find( hash ); it != m_drive.m_torrentHandleMap.end())
                {
                    it->second.m_isUsed = true;
                } else
                {
                    LOG( "markUsedFiles: internal error" );
                }
            }
        }
    }

    void sendSingleApprovalTransaction( const ApprovalTransactionInfo& singleTx )
    {
        DBG_MAIN_THREAD

        m_drive.m_eventHandler.singleModifyApprovalTransactionIsReady( m_drive.m_replicator, singleTx );
    }

    void saveSingleApprovalTransaction( const std::optional<ApprovalTransactionInfo>& singleTx )
    {
        m_drive.m_serializer.saveRestartValue( singleTx, "myOpinion" );
    }

    std::optional<ApprovalTransactionInfo> loadSingleApprovalTransaction()
    {
        std::optional<ApprovalTransactionInfo> singleTx;
        m_drive.m_serializer.loadRestartValue( singleTx, "myOpinion" );

        return singleTx;
    }
};

std::unique_ptr<DriveTaskBase> createDriveInitializationTask( std::vector<CompletedModification>&&,
                                                              DriveParams& drive,
                                                              ModifyOpinionController& opinionTaskController );

std::unique_ptr<DriveTaskBase> createModificationTask(
        mobj<ModificationRequest>&& request,
        std::map<std::array<uint8_t, 32>, ApprovalTransactionInfo>&& receivedOpinions,
        DriveParams& drive,
        ModifyOpinionController& opinionTaskController );

std::unique_ptr<DriveTaskBase> createStreamTask( mobj<StreamRequest>&& request,
                                                 DriveParams& drive,
                                                 ModifyOpinionController& opinionTaskController );

std::unique_ptr<DriveTaskBase> createCatchingUpTask( mobj<CatchingUpRequest>&& request,
                                                     DriveParams& drive,
                                                     ModifyOpinionController& opinionTaskController );

std::unique_ptr<DriveTaskBase> createModificationCancelTask( mobj<ModificationCancelRequest>&& request,
                                                             DriveParams& drive,
                                                             ModifyOpinionController& opinionTaskController );

std::unique_ptr<DriveTaskBase> createDriveClosureTask( mobj<DriveClosureRequest>&& request,
                                                       DriveParams& drive );

std::unique_ptr<DriveTaskBase> createManualModificationsTask( mobj<InitiateModificationsRequest>&& request,
                                                              DriveParams& drive,
                                                               ModifyOpinionController& opinionTaskController );


std::unique_ptr<DriveTaskBase> createManualSynchronizationTask( mobj<SynchronizationRequest>&& request,
                                                                DriveParams& drive,
                                                                ModifyOpinionController& opinionTaskController );

std::shared_ptr<DriveTaskBase> createDriveVerificationTask( mobj<VerificationRequest>&& request,
                                                            std::vector<VerifyApprovalTxInfo>&& receivedOpinions,
                                                            std::vector<VerificationCodeInfo>&& receivedCodes,
                                                            DriveParams& drive );

}

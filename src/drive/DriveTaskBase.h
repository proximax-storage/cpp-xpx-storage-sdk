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

#undef DBG_MAIN_THREAD
//#define DBG_MAIN_THREAD { assert( m_dbgThreadId == std::this_thread::get_id() ); }
#define DBG_MAIN_THREAD { _FUNC_ENTRY(); assert( m_dbgThreadId == std::this_thread::get_id() ); }
#define DBG_BG_THREAD { assert( m_dbgThreadId != std::this_thread::get_id() ); }

namespace sirius::drive
{

enum class DriveTaskType
{
    DRIVE_INITIALIZATION,
    DRIVE_CLOSURE,
    MODIFICATION_CANCEL,
    CATCHING_UP,
    MODIFICATION_REQUEST,
    DRIVE_VERIFICATION
};

class DriveTaskBase
{

private:

    const DriveTaskType m_type;

protected:

    DriveParams&    m_drive;

    std::thread::id m_dbgThreadId;
    std::string     m_dbgOurPeerName;

public:

    DriveTaskBase(
            const DriveTaskType& type,
            DriveParams& drive)
            : m_type( type ), m_drive( drive )
            , m_dbgThreadId( std::this_thread::get_id())
            , m_dbgOurPeerName(  m_drive.m_dbgOurPeerName )
    {}

    virtual ~DriveTaskBase() = default;

    virtual void run() = 0;

    virtual void terminate() = 0;

    DriveTaskType getTaskType()
    {
        DBG_MAIN_THREAD

        return m_type;
    }

    // Returns 'true' if 'CatchingUp' should be started
    virtual bool onApprovalTxPublished( const PublishedModificationApprovalTransactionInfo& transaction )
    {
        DBG_MAIN_THREAD

        return false;
    }

    virtual void onAapprovalTxFailed( const Hash256 &transactionHash )
    {
        DBG_MAIN_THREAD
    }

    virtual bool shouldCancelModify( const ModificationCancelRequest& cancelRequest )
    {
        DBG_MAIN_THREAD

        return false;
    }

    virtual void onDriveClose( const DriveClosureRequest& closureRequest )
    {
        DBG_MAIN_THREAD
    }

    virtual bool processedModifyOpinion( const ApprovalTransactionInfo& anOpinion )
    {
        DBG_MAIN_THREAD

        return false;
    }

    virtual bool processedVerificationOpinion( const VerifyApprovalTxInfo&     transactionInfo )
    {
        DBG_MAIN_THREAD

        return false;
    }

    virtual bool processedVerificationCode( const VerificationCodeInfo& info )
    {
        DBG_MAIN_THREAD

        return false;
    }

    virtual void cancelVerification( const Hash256& canceledVerification )
    {
        DBG_MAIN_THREAD
    }

protected:


    virtual void finishTask()
    {
        DBG_MAIN_THREAD

        m_drive.executeOnBackgroundThread( [this]
        {
           DBG_BG_THREAD

           std::error_code err;

           if ( !fs::exists( m_drive.m_sandboxRootPath, err ))
           {
               fs::create_directories( m_drive.m_sandboxRootPath );
           } else
           {
               for ( const auto& entry : std::filesystem::directory_iterator(
                       m_drive.m_sandboxRootPath ))
               {
                   fs::remove_all( entry.path(), err );
               }
           }

           m_drive.executeOnSessionThread( [this]
                                           {
                                               m_drive.runNextTask();
                                           } );
        } );
    }

    // Recursively marks 'm_toBeRemoved' as false
    //
    void markUsedFiles( const Folder& folder )
    {
        DBG_MAIN_THREAD

        for ( const auto& child : folder.childs() )
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

std::unique_ptr<DriveTaskBase> createDriveInitializationTask( DriveParams& drive,
                                                              ModifyOpinionController& opinionTaskController );

std::unique_ptr<DriveTaskBase> createModificationTask(
        mobj<ModificationRequest>&& request,
        std::map<std::array<uint8_t,32>,ApprovalTransactionInfo>&& receivedOpinions,
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

std::unique_ptr<DriveTaskBase> createDriveVerificationTask( mobj<VerificationRequest>&& request,
                                                            std::vector<VerifyApprovalTxInfo>&& receivedOpinions,
                                                            std::vector<VerificationCodeInfo>&& receivedCodes,
                                                            DriveParams& drive );

}

//
//  RpcReplicatorCommands.h
//  SyncRpc
//
//  Created by Aleksander Tsarenko on 14.07.22.
//

#pragma once
#include <iostream>
#include "enumToString.h"

#ifndef RPC_TEST
#include "drive/log.h"
#else
inline std::mutex gLogMutex;

// __LOG
#define __LOG(expr) { \
        const std::lock_guard<std::mutex> autolock( gLogMutex ); \
        std::cout << expr << std::endl << std::flush; \
    }
#endif

DECLARE_ENUM16 ( RPC_CMD,

    // to remote replicator
    createReplicator,
    destroyReplicator,
    start,
    asyncInitializationFinished,
    asyncModify,
    asyncCancelModify,
    asyncAddDownloadChannelInfo,
    asyncInitiateDownloadApprovalTransactionInfo,
    asyncRemoveDownloadChannelInfo,
    asyncIncreaseDownloadChannelSize,
    asyncAddDrive,
    asyncRemoveDrive,
    asyncCloseDrive,
    asyncStartDriveVerification,
    asyncCancelDriveVerification,
    asyncSetReplicators,
    asyncSetShardDonator,
    asyncSetShardRecipient,
    asyncSetChanelShard,
    asyncApprovalTransactionHasBeenPublished,
    asyncSingleApprovalTransactionHasBeenPublished,
    asyncDownloadApprovalTransactionHasBeenPublished,
    asyncVerifyApprovalTransactionHasBeenPublished,
                
    // from remote replicator
    done,
    verificationTransactionIsReady,
    modifyApprovalTransactionIsReady,
    singleModifyApprovalTransactionIsReady,
    downloadApprovalTransactionIsReady,
    opinionHasBeenReceived,
    downloadOpinionHasBeenReceived,
    onLibtorrentSessionError,

    UP_CHANNEL_INIT,
    DOWN_CHANNEL_INIT,
    READY_TO_USE,
    PING,

    // for debugging
    driveModificationIsCompleted,
    rootHashIsCalculated,
    willBeTerminated,
    driveAdded,
    driveIsInitialized,
    driveIsClosed,
    driveIsRemoved,
    driveModificationIsCanceled,
    modifyTransactionEndedWithError,
                
    dbgCrash
);

#define CMD_STR(x) enum_to_string(RPC_CMD,x)

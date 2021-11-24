/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include <string>
#include <memory>
#include <crypto/KeyPair.h>
#include "types.h"
#include "RpcTypes.h"
#include "rpc/client.h"
#include "rpc/server.h"
#include "rpc/this_handler.h"

namespace sirius::drive {
    class PLUGIN_API ExtensionEmulator {
    public:
        ExtensionEmulator(const std::string& address, const unsigned short& port);
        virtual ~ExtensionEmulator() = default;

    public:
        void run();

    private:
        void bindOperations();

    protected:

        virtual void openDownloadChannel(types::RpcDownloadChannelInfo& channelInfo);

        virtual void closeDownloadChannel(const std::array<uint8_t,32>& channelKey);

        virtual void modifyDrive(types::RpcDataModification& rpcDataModification, const types::RpcClientInfo& rpcClientInfo);

        virtual void modifyApproveTransactionIsReady(const types::RpcModifyApprovalTransactionInfo& rpcModifyApprovalTransactionInfo);

        virtual void singleModifyApproveTransactionIsReady(const types::RpcModifyApprovalTransactionInfo& rpcModifyApprovalTransactionInfo);

        virtual void driveModificationIsCompleted(const types::RpcEndDriveModificationInfo& rpcEndDriveModificationInfo);

        virtual void downloadApproveTransactionIsReady();

        virtual void prepareDriveTransaction(types::RpcPrepareDriveTransactionInfo& rpcPrepareDriveTransactionInfo);

        virtual void driveClosureTransaction(const std::array<uint8_t, 32>& driveKey);

        virtual void replicatorOnboardingTransaction(const types::RpcReplicatorInfo& rpcReplicatorInfo);

        virtual void driveAdded(const std::array<uint8_t,32>& drivePubKey);

        types::RpcDriveInfo getDrive(const std::array<uint8_t,32>& drivePubKey);

    protected:
        std::map<std::array<uint8_t,32>, types::RpcClientInfo> m_endDriveModificationHashes;
        std::map<std::array<uint8_t,32>, unsigned long> m_endDriveModificationCounter;
        std::map<std::array<uint8_t,32>, types::RpcClientInfo> m_addedDrives;
        std::map<std::array<uint8_t,32>, unsigned long> m_addedDrivesCounter;
        std::set<std::array<uint8_t,32>> m_modifyApproveHashes;
        std::shared_ptr<rpc::server> m_rpcServer;
        types::RpcReplicatorList m_rpcReplicators;
        std::string m_address;
        unsigned short m_port;
    };
}
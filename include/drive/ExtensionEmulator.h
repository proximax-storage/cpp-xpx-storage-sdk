/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include <string>
#include <memory>
#include "types.h"
#include "RpcTypes.h"
#include "rpc/client.h"
#include "rpc/server.h"
#include "rpc/this_handler.h"

namespace sirius::drive {
    class ExtensionEmulator {
    public:
        ExtensionEmulator(const std::string& address, const std::string& port) {
            m_rpcServer = std::make_shared<rpc::server>( address, std::stoi(port) );
            bindOperations();
        }

        ~ExtensionEmulator() = default;

    public:
        void run() {
            std::cout << "ExtensionEmulator is started" << std::endl;
            m_rpcServer->run();
        }

    private:
        void bindOperations() {
            // Incoming operations from a clients
            m_rpcServer->bind("openDownloadChannel", [this](types::RpcDownloadChannelInfo& rpcDownloadChannelInfo) {
                openDownloadChannel(rpcDownloadChannelInfo);
            });

            m_rpcServer->bind("DataModificationTransaction", [this](const types::RpcDataModification& rpcDataModification) {
                modifyDrive(rpcDataModification);
            });

            // Incoming operations from a replicators
            m_rpcServer->bind("modifyApproveTransactionIsReady", [this](const types::RpcModifyApprovalTransactionInfo& rpcModifyApprovalTransactionInfo) {
                modifyApproveTransactionIsReady(rpcModifyApprovalTransactionInfo);
            });

            m_rpcServer->bind("singleModifyApproveTransactionIsReady", [this](const types::RpcModifyApprovalTransactionInfo& rpcModifyApprovalTransactionInfo) {
                singleModifyApproveTransactionIsReady(rpcModifyApprovalTransactionInfo);
            });

            m_rpcServer->bind("driveModificationIsCompleted", [this]() {
                driveModificationIsCompleted();
            });

            m_rpcServer->bind("downloadApproveTransactionIsReady", [this]() {
                downloadApproveTransactionIsReady();
            });
        }

        void openDownloadChannel(types::RpcDownloadChannelInfo& channelInfo) {
            // TODO: random choose a replicators group
            for (const types::RpcReplicatorInfo& rpcReplicatorInfo : m_rpcReplicators) {

                const std::string address = rpcReplicatorInfo.m_rpcReplicatorAddress;
                const unsigned short port = rpcReplicatorInfo.m_rpcReplicatorPort;

                rpc::client replicator(address, port);
                replicator.call("openDownloadChannel", channelInfo);
            }
        }

        void modifyDrive(const types::RpcDataModification& rpcDataModification) {
            for (const types::RpcReplicatorInfo& rpcReplicatorInfo : m_rpcReplicators) {

                const std::string address = rpcReplicatorInfo.m_rpcReplicatorAddress;
                const unsigned short port = rpcReplicatorInfo.m_rpcReplicatorPort;

                rpc::client replicator(address, port);
                replicator.call("modifyDrive", rpcDataModification);
            }
        }

        void modifyApproveTransactionIsReady(const types::RpcModifyApprovalTransactionInfo& rpcModifyApprovalTransactionInfo) {
            for (const types::RpcReplicatorInfo& rpcReplicatorInfo : m_rpcReplicators) {

                const std::string address = rpcReplicatorInfo.m_rpcReplicatorAddress;
                const unsigned short port = rpcReplicatorInfo.m_rpcReplicatorPort;

                rpc::client replicator(address, port);
                replicator.call("acceptModifyApprovalTransaction", rpcModifyApprovalTransactionInfo);
            }
        }

        void singleModifyApproveTransactionIsReady(const types::RpcModifyApprovalTransactionInfo& rpcModifyApprovalTransactionInfo) {
            for (const types::RpcReplicatorInfo& rpcReplicatorInfo : m_rpcReplicators) {

                const std::string address = rpcReplicatorInfo.m_rpcReplicatorAddress;
                const unsigned short port = rpcReplicatorInfo.m_rpcReplicatorPort;

                rpc::client replicator(address, port);
                replicator.call("acceptSingleModifyApprovalTransaction", rpcModifyApprovalTransactionInfo);
            }
        }

        void driveModificationIsCompleted() {
            std::cout << "driveModificationIsCompleted" << std::endl;
        }

        void downloadApproveTransactionIsReady() {
        }

    private:
        std::shared_ptr<rpc::server> m_rpcServer;
        types::RpcReplicatorList m_rpcReplicators;
    };
}
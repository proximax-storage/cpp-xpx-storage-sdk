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
    class ExtensionEmulator {
    public:
        ExtensionEmulator(const std::string& address, const unsigned short& port) {
            m_address = address;
            m_port = port;
            m_rpcServer = std::make_shared<rpc::server>( address, port );
            bindOperations();
        }

        ~ExtensionEmulator() = default;

    public:
        void run() {
            std::cout << "ExtensionEmulator is started: " << m_address << ":" << m_port << std::endl;
            m_rpcServer->run();
        }

    private:
        void bindOperations() {
            // Incoming operations from a clients
            m_rpcServer->bind("PrepareDriveTransaction", [this](types::RpcPrepareDriveTransactionInfo& rpcPrepareDriveTransactionInfo) {
                prepareDriveTransaction(rpcPrepareDriveTransactionInfo);
            });

            m_rpcServer->bind("DriveClosureTransaction", [this](const std::array<uint8_t, 32>& driveKey) {
                driveClosureTransaction(driveKey);
            });

            m_rpcServer->bind("openDownloadChannel", [this](types::RpcDownloadChannelInfo& rpcDownloadChannelInfo) {
                openDownloadChannel(rpcDownloadChannelInfo);
            });

            m_rpcServer->bind("closeDownloadChannel", [this](const std::array<uint8_t,32>& channelKey) {
                closeDownloadChannel(channelKey);
            });

            m_rpcServer->bind("DataModificationTransaction", [this](types::RpcDataModification& rpcDataModification) {
                modifyDrive(rpcDataModification);
            });

            m_rpcServer->bind("drive", [this](const std::array<uint8_t,32>& drivePubKey) {
                return getDrive(drivePubKey);
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

            m_rpcServer->bind("ReplicatorOnboardingTransaction", [this](const types::RpcReplicatorInfo& rpcReplicatorInfo) {
                replicatorOnboardingTransaction(rpcReplicatorInfo);
            });
        }

        void openDownloadChannel(types::RpcDownloadChannelInfo& channelInfo) {
            std::cout << "Extension. openDownloadChannel: " << utils::HexFormat(channelInfo) << std::endl;

            if(m_rpcReplicators.empty()){
                std::cout << "Extension. openDownloadChannel. No replicators found! " << utils::HexFormat(channelInfo) << std::endl;
            }

            // TODO: random choose a replicators group
            for (const types::RpcReplicatorInfo& rpcReplicatorInfo : m_rpcReplicators) {

                const std::string address = rpcReplicatorInfo.m_replicatorAddress;
                const unsigned short port = rpcReplicatorInfo.m_rpcReplicatorPort;

                rpc::client replicator(address, port);
                replicator.call("openDownloadChannel", channelInfo);
            }
        }

        void closeDownloadChannel(const std::array<uint8_t,32>& channelKey) {
            std::cout << "Extension. closeDownloadChannel: " << utils::HexFormat(channelKey) << std::endl;

            if(m_rpcReplicators.empty()){
                std::cout << "Extension. closeDownloadChannel. No replicators found! " << utils::HexFormat(channelKey) << std::endl;
            }

            // TODO: choose a replicators group
            for (const types::RpcReplicatorInfo& rpcReplicatorInfo : m_rpcReplicators) {

                const std::string address = rpcReplicatorInfo.m_replicatorAddress;
                const unsigned short port = rpcReplicatorInfo.m_rpcReplicatorPort;

                rpc::client replicator(address, port);
                replicator.call("closeDownloadChannel", channelKey);
            }
        }

        void modifyDrive(types::RpcDataModification& rpcDataModification) {
            std::cout << "Extension. modifyDrive: " << utils::HexFormat(rpcDataModification.m_drivePubKey) << std::endl;

            if(m_rpcReplicators.empty()){
                std::cout << "Extension. modifyDrive. No replicators found! " << utils::HexFormat(rpcDataModification.m_drivePubKey) << std::endl;
            }

            // TODO: choose a replicators group
            rpcDataModification.m_rpcReplicators = m_rpcReplicators;

            for (const types::RpcReplicatorInfo& rpcReplicatorInfo : m_rpcReplicators) {

                const std::string address = rpcReplicatorInfo.m_replicatorAddress;
                const unsigned short port = rpcReplicatorInfo.m_rpcReplicatorPort;

                rpc::client replicator(address, port);
                replicator.call("modifyDrive", rpcDataModification);
            }
        }

        void modifyApproveTransactionIsReady(const types::RpcModifyApprovalTransactionInfo& rpcModifyApprovalTransactionInfo) {
            std::cout << "Extension. modifyApproveTransactionIsReady: " << utils::HexFormat(rpcModifyApprovalTransactionInfo.m_drivePubKey) << std::endl;

            if (!m_modifyApproveHashes.contains(rpcModifyApprovalTransactionInfo.m_modifyTransactionHash)) {
                m_modifyApproveHashes.insert(rpcModifyApprovalTransactionInfo.m_modifyTransactionHash);

                for (const types::RpcReplicatorInfo& rpcReplicatorInfo : m_rpcReplicators) {
                    const std::string address = rpcReplicatorInfo.m_replicatorAddress;
                    const unsigned short port = rpcReplicatorInfo.m_rpcReplicatorPort;

                    rpc::client replicator(address, port);
                    replicator.call("acceptModifyApprovalTransaction", rpcModifyApprovalTransactionInfo);
                }
            }
        }

        void singleModifyApproveTransactionIsReady(const types::RpcModifyApprovalTransactionInfo& rpcModifyApprovalTransactionInfo) {
            std::cout << "Extension. singleModifyApproveTransactionIsReady: " << utils::HexFormat(rpcModifyApprovalTransactionInfo.m_drivePubKey) << std::endl;

            types::RpcReplicatorInfo rpcReplicatorInfo;
            rpcReplicatorInfo.m_replicatorPubKey = rpcModifyApprovalTransactionInfo.m_replicatorPubKey;

            const auto& r = std::find(m_rpcReplicators.begin(), m_rpcReplicators.end(), rpcReplicatorInfo);
            if(r != m_rpcReplicators.end()) {
                const std::string address = r->m_replicatorAddress;
                const unsigned short port = r->m_rpcReplicatorPort;

                rpc::client replicator(address, port);
                replicator.call("acceptSingleModifyApprovalTransaction", rpcModifyApprovalTransactionInfo);
            } else {
                std::cout << "Extension. replicator not found!: " << utils::HexFormat(rpcModifyApprovalTransactionInfo.m_replicatorPubKey) << std::endl;
            }
        }

        void driveModificationIsCompleted() {
            std::cout << "Extension. driveModificationIsCompleted" << std::endl;
        }

        void downloadApproveTransactionIsReady() {
        }

        void prepareDriveTransaction(types::RpcPrepareDriveTransactionInfo& rpcPrepareDriveTransactionInfo) {
            std::cout << "Extension. prepareDriveTransaction. Drive key: " << utils::HexFormat(rpcPrepareDriveTransactionInfo.m_driveKey) << std::endl;

            if(m_rpcReplicators.empty()){
                std::cout << "Extension. prepareDriveTransaction. No replicators found! " << utils::HexFormat(rpcPrepareDriveTransactionInfo.m_driveKey) << std::endl;
            }

            // TODO: random choose a replicators group
            rpcPrepareDriveTransactionInfo.m_rpcReplicators = m_rpcReplicators;

            for (const types::RpcReplicatorInfo& rpcReplicatorInfo : m_rpcReplicators) {

                const std::string address = rpcReplicatorInfo.m_replicatorAddress;
                const unsigned short port = rpcReplicatorInfo.m_rpcReplicatorPort;

                rpc::client replicator(address, port);
                replicator.call("PrepareDriveTransaction", rpcPrepareDriveTransactionInfo);
            }
        }

        void driveClosureTransaction(const std::array<uint8_t, 32>& driveKey) {
            std::cout << "Extension. driveClosureTransaction. Drive key: " << utils::HexFormat(driveKey) << std::endl;

            if(m_rpcReplicators.empty()){
                std::cout << "Extension. driveClosureTransaction. No replicators found! " << utils::HexFormat(driveKey) << std::endl;
            }

            for (const types::RpcReplicatorInfo& rpcReplicatorInfo : m_rpcReplicators) {

                const std::string address = rpcReplicatorInfo.m_replicatorAddress;
                const unsigned short port = rpcReplicatorInfo.m_rpcReplicatorPort;

                rpc::client replicator(address, port);
                replicator.call("DriveClosureTransaction", driveKey);
            }
        }

        void replicatorOnboardingTransaction(const types::RpcReplicatorInfo& rpcReplicatorInfo) {
            std::cout << "Extension. replicatorOnboardingTransaction. Replicator key: " << utils::HexFormat(rpcReplicatorInfo.m_replicatorPubKey) << std::endl;

            if(std::find(m_rpcReplicators.begin(), m_rpcReplicators.end(), rpcReplicatorInfo) != m_rpcReplicators.end()) {
                std::cout << "Extension. replicatorOnboardingTransaction. Replicator exists: " << utils::HexFormat(rpcReplicatorInfo.m_replicatorPubKey) << std::endl;
            } else {
                m_rpcReplicators.push_back(rpcReplicatorInfo);
            }
        }

        types::RpcDriveInfo getDrive(const std::array<uint8_t,32>& drivePubKey) {
            std::cout << "Extension. getDrive. Replicator key: " << utils::HexFormat(drivePubKey) << std::endl;
            if(m_rpcReplicators.empty()){
                std::cout << "Extension. getDrive. No replicators found! " << utils::HexFormat(drivePubKey) << std::endl;
            }
            else {
                const std::string address = m_rpcReplicators[0].m_replicatorAddress;
                const unsigned short port = m_rpcReplicators[0].m_rpcReplicatorPort;

                rpc::client replicator(address, port);
                return replicator.call("drive", drivePubKey).as<types::RpcDriveInfo>();
            }

            return {};
        }

    private:
        std::set<std::array<uint8_t,32>> m_modifyApproveHashes;
        std::shared_ptr<rpc::server> m_rpcServer;
        types::RpcReplicatorList m_rpcReplicators;
        std::string m_address;
        unsigned short m_port;
    };
}
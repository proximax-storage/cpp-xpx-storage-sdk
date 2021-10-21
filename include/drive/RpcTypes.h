/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "types.h"
#include "rpc/msgpack/adaptor/define_decl.hpp"
#include "rpc/msgpack.hpp"
#include "FlatDrive.h"


namespace sirius::drive::types {

        struct RpcReplicatorInfo
        {
            bool operator==(const RpcReplicatorInfo& replicator) const {
                return replicator.m_replicatorPubKey == m_replicatorPubKey;
            }

            std::string             m_replicatorAddress;
            unsigned short          m_replicatorPort;
            std::array<uint8_t,32>  m_replicatorPubKey;
            unsigned short          m_rpcReplicatorPort;
            MSGPACK_DEFINE_ARRAY(m_replicatorAddress, m_replicatorPort, m_replicatorPubKey, m_rpcReplicatorPort);
        };

        using RpcReplicatorList = std::vector<RpcReplicatorInfo>;

        struct RpcDownloadChannelInfo {
            std::vector<Key> getClientsPublicKeys() const {
                std::vector<Key> clientsPublicKeys(m_clientsPublicKeys.size());
                std::copy(m_clientsPublicKeys.begin(), m_clientsPublicKeys.end(), clientsPublicKeys.begin());

                return clientsPublicKeys;
            }

            void setClientsPublicKeys(const std::vector<Key>& clientsPublicKeys) {
                for (const Key& key : clientsPublicKeys) {
                    m_clientsPublicKeys.emplace_back(key.array());
                }
            }

            ReplicatorList getReplicators() const {
                ReplicatorList replicators;
                for (const RpcReplicatorInfo& rpcReplicatorInfo : m_rpcReplicators) {
                    boost::asio::ip::address address = boost::asio::ip::address::from_string(rpcReplicatorInfo.m_replicatorAddress);
                    replicators.emplace_back( drive::ReplicatorInfo{ {
                        address,
                        rpcReplicatorInfo.m_replicatorPort
                    },  rpcReplicatorInfo.m_replicatorPubKey } );
                }

                return replicators;
            }

            std::array<uint8_t, 32>                 m_channelKey;
            size_t                                  m_prepaidDownloadSize;
            RpcReplicatorList                       m_rpcReplicators;
            std::vector<std::array<uint8_t, 32>>    m_clientsPublicKeys;
            MSGPACK_DEFINE_ARRAY(m_channelKey, m_prepaidDownloadSize, m_rpcReplicators, m_clientsPublicKeys);
        };

        struct RpcDataModification {
            ReplicatorList getReplicators() const {
                ReplicatorList replicators;
                for (const RpcReplicatorInfo& rpcReplicatorInfo : m_rpcReplicators) {
                    boost::asio::ip::address address = boost::asio::ip::address::from_string(rpcReplicatorInfo.m_replicatorAddress);
                    replicators.emplace_back( drive::ReplicatorInfo{ {
                            address,
                            rpcReplicatorInfo.m_replicatorPort
                        },  rpcReplicatorInfo.m_replicatorPubKey } );
                }

                return replicators;
            }

            std::array<uint8_t,32>  m_drivePubKey;
            std::array<uint8_t,32>  m_clientPubKey;
            std::array<uint8_t,32>  m_infoHash;
            std::array<uint8_t,32>  m_transactionHash;
            uint64_t                m_maxDataSize;
            RpcReplicatorList       m_rpcReplicators;
            MSGPACK_DEFINE_ARRAY(m_drivePubKey, m_clientPubKey, m_infoHash, m_transactionHash, m_maxDataSize, m_rpcReplicators);
        };

        struct RpcSingleOpinion {
            // Replicator public key
            std::array<uint8_t,32>  m_replicatorKey;

            // Opinions about how much the Replicators and the Drive Owner have uploaded to this Replicator.
            //TODO
            std::vector<uint8_t>    m_uploadReplicatorKeys;
            std::vector<uint64_t>   m_replicatorUploadBytes;
            uint64_t                m_clientUploadBytes = 0;

            // Signature of { modifyTransactionHash, rootHash, replicatorsUploadBytes, clientUploadBytes }
            std::array<uint8_t,64>  m_signature;
            MSGPACK_DEFINE_ARRAY(m_replicatorKey, m_uploadReplicatorKeys, m_replicatorUploadBytes, m_clientUploadBytes, m_signature);
        };

        struct RpcModifyApprovalTransactionInfo {
            ApprovalTransactionInfo getApprovalTransactionInfo() const {
                ApprovalTransactionInfo approvalTransactionInfo;
                approvalTransactionInfo.m_driveKey = m_drivePubKey;
                approvalTransactionInfo.m_modifyTransactionHash = m_modifyTransactionHash;
                approvalTransactionInfo.m_rootHash = m_rootHash;
                approvalTransactionInfo.m_fsTreeFileSize = m_fsTreeFileSize;
                approvalTransactionInfo.m_metaFilesSize = m_metaFilesSize;
                approvalTransactionInfo.m_driveSize = m_driveSize;

                for (const RpcSingleOpinion& rpcOpinion : m_opinions) {
                    SingleOpinion singleOpinion;
                    singleOpinion.m_replicatorKey = rpcOpinion.m_replicatorKey;
                    singleOpinion.m_clientUploadBytes = rpcOpinion.m_clientUploadBytes;
                    singleOpinion.m_uploadReplicatorKeys = rpcOpinion.m_uploadReplicatorKeys;
                    singleOpinion.m_replicatorUploadBytes = rpcOpinion.m_replicatorUploadBytes;
                    singleOpinion.m_signature = rpcOpinion.m_signature;
                    approvalTransactionInfo.m_opinions.push_back(singleOpinion);
                }

                return approvalTransactionInfo;
            }

            static RpcModifyApprovalTransactionInfo getRpcModifyApprovalTransactionInfo(const std::array<uint8_t,32>& replicatorPubKey, ApprovalTransactionInfo&& transactionInfo) {
                types::RpcModifyApprovalTransactionInfo rpcModifyApprovalTransactionInfo;
                rpcModifyApprovalTransactionInfo.m_drivePubKey = transactionInfo.m_driveKey;
                rpcModifyApprovalTransactionInfo.m_replicatorPubKey = replicatorPubKey;
                rpcModifyApprovalTransactionInfo.m_modifyTransactionHash = transactionInfo.m_modifyTransactionHash;
                rpcModifyApprovalTransactionInfo.m_rootHash = transactionInfo.m_rootHash;
                rpcModifyApprovalTransactionInfo.m_fsTreeFileSize = transactionInfo.m_fsTreeFileSize;
                rpcModifyApprovalTransactionInfo.m_metaFilesSize = transactionInfo.m_metaFilesSize;
                rpcModifyApprovalTransactionInfo.m_driveSize = transactionInfo.m_driveSize;
                rpcModifyApprovalTransactionInfo.setOpinions(transactionInfo.m_opinions);

                return rpcModifyApprovalTransactionInfo;
            }

            void setOpinions(const std::vector<SingleOpinion>& opinions) {
                for (const SingleOpinion& opinion : opinions) {
                    RpcSingleOpinion rpcSingleOpinion;
                    rpcSingleOpinion.m_replicatorKey = opinion.m_replicatorKey;
                    rpcSingleOpinion.m_clientUploadBytes = opinion.m_clientUploadBytes;
                    rpcSingleOpinion.m_uploadReplicatorKeys = opinion.m_uploadReplicatorKeys;
                    rpcSingleOpinion.m_replicatorUploadBytes = opinion.m_replicatorUploadBytes;
                    rpcSingleOpinion.m_signature = opinion.m_signature.array();
                    m_opinions.push_back(rpcSingleOpinion);
                }
            }

            // Drive public key
            std::array<uint8_t,32>  m_drivePubKey;

            // Replicator public key
            std::array<uint8_t,32>  m_replicatorPubKey;

            // A reference to the transaction that initiated the modification
            std::array<uint8_t,32>  m_modifyTransactionHash;

            // Content Download Information for the File Structure
            std::array<uint8_t,32>  m_rootHash;

            // The size of the “File Structure” File
            uint64_t                m_fsTreeFileSize;

            // The size of metafiles (torrents?,folders?) including “File Structure” File
            uint64_t                m_metaFilesSize;

            // Total used disk space. Must not be more than the Drive Size.
            uint64_t                m_driveSize;

            // Opinions about how much the Replicators and the Drive Owner have uploaded to this Replicator.
            std::vector<RpcSingleOpinion>   m_opinions;
            MSGPACK_DEFINE_ARRAY(m_drivePubKey, m_replicatorPubKey, m_modifyTransactionHash, m_rootHash, m_fsTreeFileSize, m_metaFilesSize, m_driveSize, m_opinions);
        };

        struct RpcPrepareDriveTransactionInfo {
            ReplicatorList getReplicators() const {
                ReplicatorList replicators;
                for (const RpcReplicatorInfo& rpcReplicatorInfo : m_rpcReplicators) {
                    boost::asio::ip::address address = boost::asio::ip::address::from_string(rpcReplicatorInfo.m_replicatorAddress);
                    replicators.emplace_back( drive::ReplicatorInfo{
                        { address, rpcReplicatorInfo.m_replicatorPort },
                          rpcReplicatorInfo.m_replicatorPubKey } );
                }

                return replicators;
            }

            std::array<uint8_t,32>  m_clientPubKey;
            std::array<uint8_t,32>  m_driveKey; // The Drive Key equals the hash of the PrepareDriveTransaction
            uint64_t                m_driveSize;
            std::array<uint8_t,64>  m_signature;
            RpcReplicatorList       m_rpcReplicators; // add on extension side
            MSGPACK_DEFINE_ARRAY(m_clientPubKey, m_driveKey, m_driveSize, m_signature, m_rpcReplicators);
        };

    struct RpcDriveInfo {
        void setReplicators(const ReplicatorList& replicators) {
            for (const ReplicatorInfo& replicator : replicators) {
                RpcReplicatorInfo rpcReplicatorInfo;
                rpcReplicatorInfo.m_replicatorAddress = replicator.m_endpoint.address().to_string();
                rpcReplicatorInfo.m_replicatorPort = replicator.m_endpoint.port();
                rpcReplicatorInfo.m_replicatorPubKey = replicator.m_publicKey.array();

                // TODO: pass as argument
                rpcReplicatorInfo.m_rpcReplicatorPort = 5510;

                m_rpcReplicators.push_back(rpcReplicatorInfo);
            }
        }

        ReplicatorList getReplicators() const {
            ReplicatorList replicators;
            for (const RpcReplicatorInfo& rpcReplicatorInfo : m_rpcReplicators) {
                boost::asio::ip::address address = boost::asio::ip::address::from_string(rpcReplicatorInfo.m_replicatorAddress);
                replicators.emplace_back( drive::ReplicatorInfo{
                        { address, rpcReplicatorInfo.m_replicatorPort },
                        rpcReplicatorInfo.m_replicatorPubKey } );
            }

            return replicators;
        }

        std::array<uint8_t,32>                  m_driveKey;
        std::array<uint8_t,32>                  m_rootHash;
        std::vector<std::array<uint8_t, 32>>    m_clientsPublicKeys;
        RpcReplicatorList                       m_rpcReplicators;
        MSGPACK_DEFINE_ARRAY(m_driveKey, m_rootHash, m_clientsPublicKeys, m_rpcReplicators);
    };
}

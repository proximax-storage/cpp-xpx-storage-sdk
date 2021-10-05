/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "types.h"
#include "rpc/msgpack/adaptor/define_decl.hpp"
#include "rpc/msgpack.hpp"


namespace sirius::drive::types {

        struct ResultWithInfoHash {
            std::array<uint8_t,32>  m_rootHash;
            std::string             m_error;
            MSGPACK_DEFINE_ARRAY(m_rootHash,m_error);
        };

        struct ResultWithModifyStatus {
            int                     m_modifyStatus;
            std::string             m_error;
            MSGPACK_DEFINE_ARRAY(m_modifyStatus,m_error);
        };

        struct RpcReplicatorInfo
        {
            std::string             m_replicatorAddress;
            unsigned short          m_replicatorPort;
            std::array<uint8_t,32>  m_replicatorPubKey;
            std::string             m_rpcReplicatorAddress;
            unsigned short          m_rpcReplicatorPort;
            MSGPACK_DEFINE_ARRAY(m_replicatorAddress, m_replicatorPort, m_replicatorPubKey, m_rpcReplicatorAddress, m_rpcReplicatorPort);
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
//
//            void setReplicators(const RpcReplicatorList& replicators) {
//                for (const RpcReplicatorInfo& rpcReplicatorInfo : replicators) {
//                    RpcReplicatorInfo info;
//                    info.replicatorInfo = rpcReplicatorInfo.replicatorInfo;
//                    info.
//                    info.m_address = replicatorInfo.m_endpoint.address().to_string();
//                    info.m_port = replicatorInfo.m_endpoint.port();
//
//                    std::copy(replicatorInfo.m_publicKey.begin(), replicatorInfo.m_publicKey.end(), info.m_publicKey.begin());
//                    m_replicators.emplace_back(info);
//                }
//            }

//            struct ReplicatorInfo {
//                std::array<uint8_t, 32> m_publicKey;
//                std::string             m_address;
//                unsigned short          m_port;
//                MSGPACK_DEFINE_ARRAY(m_publicKey, m_address, m_port);
//            };

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
            std::vector<uint8_t>    m_replicatorKeys;
            std::vector<uint64_t>   m_replicatorUploadBytes;
            uint64_t                m_clientUploadBytes = 0;

            // Signature of { modifyTransactionHash, rootHash, replicatorsUploadBytes, clientUploadBytes }
            std::array<uint8_t,64>  m_signature;
            MSGPACK_DEFINE_ARRAY(m_replicatorKey, m_replicatorKeys, m_replicatorUploadBytes, m_clientUploadBytes, m_signature);
        };

        struct RpcModifyApprovalTransactionInfo {
            // Drive public key
            std::array<uint8_t,32>  m_drivePubKey;

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
            MSGPACK_DEFINE_ARRAY(m_drivePubKey, m_modifyTransactionHash, m_rootHash, m_fsTreeFileSize, m_metaFilesSize, m_driveSize, m_opinions);
        };
}

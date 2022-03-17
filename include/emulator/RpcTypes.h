/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "types.h"
#include "rpc/msgpack/adaptor/define_decl.hpp"
#include "rpc/msgpack.hpp"
#include "drive/FlatDrive.h"


namespace sirius::emulator::types {

    struct PLUGIN_API RpcReplicatorInfo
    {
        bool operator==(const RpcReplicatorInfo& replicator) const ;

        std::string             m_replicatorAddress;
        unsigned short          m_replicatorPort;
        std::array<uint8_t,32>  m_replicatorPubKey;
        unsigned short          m_rpcReplicatorPort;
        MSGPACK_DEFINE_ARRAY(m_replicatorAddress, m_replicatorPort, m_replicatorPubKey, m_rpcReplicatorPort);
    };

    struct PLUGIN_API RpcClientInfo
    {
        bool operator==(const RpcClientInfo& client) const;

        std::string             m_address;
        unsigned short          m_rpcPort;
        std::array<uint8_t,32>  m_clientPubKey;
        MSGPACK_DEFINE_ARRAY(m_address, m_rpcPort, m_clientPubKey);
    };

    using RpcReplicatorList = std::vector<RpcReplicatorInfo>;
    using KeysList = std::vector<std::array<uint8_t, 32>>;

    struct PLUGIN_API RpcDownloadChannelInfo {
        std::vector<Key> getClientsPublicKeys() const;

        void setClientsPublicKeys(const std::vector<Key>& clientsPublicKeys);

        drive::ReplicatorList getReplicators() const;

        std::array<uint8_t, 32>                 m_channelKey;
        size_t                                  m_prepaidDownloadSize;
        std::array<uint8_t, 32>                 m_drivePubKey;
        KeysList                                m_rpcReplicators;
        KeysList                                m_clientsPublicKeys;
        MSGPACK_DEFINE_ARRAY(m_channelKey, m_prepaidDownloadSize, m_drivePubKey, m_rpcReplicators, m_clientsPublicKeys);
    };

    struct PLUGIN_API RpcDataModification {
        drive::ReplicatorList getReplicators() const;

        std::array<uint8_t,32>  m_drivePubKey;
        std::array<uint8_t,32>  m_clientPubKey;
        std::array<uint8_t,32>  m_infoHash;
        std::array<uint8_t,32>  m_transactionHash;
        uint64_t                m_maxDataSize;
        KeysList                m_rpcReplicators;
        MSGPACK_DEFINE_ARRAY(m_drivePubKey, m_clientPubKey, m_infoHash, m_transactionHash, m_maxDataSize, m_rpcReplicators);
    };

    struct PLUGIN_API RpcKeyAndBytes {
        std::array<uint8_t,32> m_key;
        uint64_t m_uploadedBytes;
        MSGPACK_DEFINE_ARRAY(m_key, m_uploadedBytes);
    };

    struct PLUGIN_API RpcSingleOpinion {

        std::vector<drive::KeyAndBytes> getUploadLayout() const;
        void setUploadLayout(const std::vector<drive::KeyAndBytes>& uploadLayout);

        // Replicator public key
        std::array<uint8_t,32>          m_replicatorKey;

        // Opinions about how much the Replicators and the Drive Owner have uploaded to this Replicator.
        //TODO
        std::vector<RpcKeyAndBytes>     m_uploadLayout;

        // Signature of { modifyTransactionHash, rootHash, replicatorsUploadBytes, clientUploadBytes }
        std::array<uint8_t,64>  m_signature;
        MSGPACK_DEFINE_ARRAY(m_replicatorKey, m_uploadLayout, m_signature);
    };

    struct PLUGIN_API RpcModifyApprovalTransactionInfo {
        drive::ApprovalTransactionInfo getApprovalTransactionInfo() const;

        static RpcModifyApprovalTransactionInfo getRpcModifyApprovalTransactionInfo(const std::array<uint8_t,32>& replicatorPubKey, const drive::ApprovalTransactionInfo& transactionInfo);

        void setOpinions(const std::vector<drive::SingleOpinion>& opinions);

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

    struct PLUGIN_API RpcPrepareDriveTransactionInfo {
        drive::ReplicatorList getReplicators() const;

        std::array<uint8_t,32>  m_clientPubKey;
        std::array<uint8_t,32>  m_driveKey; // The Drive Key equals the hash of the PrepareDriveTransaction
        uint64_t                m_driveSize;
        std::array<uint8_t,64>  m_signature;
        KeysList                m_rpcReplicators; // add on extension side
        RpcClientInfo           m_rpcClientInfo;
        MSGPACK_DEFINE_ARRAY(m_clientPubKey, m_driveKey, m_driveSize, m_signature, m_rpcReplicators, m_rpcClientInfo);
    };

    struct PLUGIN_API RpcDriveInfo {
        void setReplicators(const drive::ReplicatorList&);

        drive::ReplicatorList getReplicators() const;

        std::array<uint8_t,32>                  m_driveKey;
        std::array<uint8_t,32>                  m_rootHash;
        std::vector<std::array<uint8_t, 32>>    m_clientsPublicKeys;
        KeysList                                m_rpcReplicators;
        MSGPACK_DEFINE_ARRAY(m_driveKey, m_rootHash, m_clientsPublicKeys, m_rpcReplicators);
    };

    struct PLUGIN_API RpcEndDriveModificationInfo {
        std::array<uint8_t,32>                  m_modifyTransactionHash;
        RpcReplicatorInfo                       m_replicatorInfo; // public key only
        MSGPACK_DEFINE_ARRAY(m_modifyTransactionHash, m_replicatorInfo);
    };
}

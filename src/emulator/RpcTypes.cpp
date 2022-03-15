/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "emulator/RpcTypes.h"


namespace sirius::emulator::types {

    bool RpcReplicatorInfo::operator==(const RpcReplicatorInfo& replicator) const {
        return replicator.m_replicatorPubKey == m_replicatorPubKey;
    }

    bool RpcClientInfo::operator==(const RpcClientInfo& client) const {
        return client.m_clientPubKey == m_clientPubKey;
    }

    std::vector<Key> RpcDownloadChannelInfo::getClientsPublicKeys() const {
        std::vector<Key> clientsPublicKeys(m_clientsPublicKeys.size());
        std::copy(m_clientsPublicKeys.begin(), m_clientsPublicKeys.end(), clientsPublicKeys.begin());

        return clientsPublicKeys;
    }

    void RpcDownloadChannelInfo::setClientsPublicKeys(const std::vector<Key>& clientsPublicKeys) {
        for (const Key& key : clientsPublicKeys) {
            m_clientsPublicKeys.emplace_back(key.array());
        }
    }

    drive::ReplicatorList RpcDownloadChannelInfo::getReplicators() const {
        return *reinterpret_cast<const drive::ReplicatorList *>(&m_rpcReplicators);
    }

    drive::ReplicatorList RpcDataModification::getReplicators() const {
        return *reinterpret_cast<const drive::ReplicatorList *>(&m_rpcReplicators);
    }

    void RpcSingleOpinion::setUploadLayout(const std::vector<drive::KeyAndBytes>& uploadLayout)
    {
        m_uploadLayout.clear();
        for (const auto& item: uploadLayout) {
            m_uploadLayout.push_back( { item.m_key, item.m_uploadedBytes } );
        }
    }

    std::vector<drive::KeyAndBytes> RpcSingleOpinion::getUploadLayout() const
    {
        std::vector<drive::KeyAndBytes> uploadLayout;
        for (const auto& item: m_uploadLayout) {
            uploadLayout.push_back( { item.m_key, item.m_uploadedBytes} );
        }
        return uploadLayout;
    }

    drive::ApprovalTransactionInfo RpcModifyApprovalTransactionInfo::getApprovalTransactionInfo() const {
        drive::ApprovalTransactionInfo approvalTransactionInfo;
        approvalTransactionInfo.m_driveKey = m_drivePubKey;
        approvalTransactionInfo.m_modifyTransactionHash = m_modifyTransactionHash;
        approvalTransactionInfo.m_rootHash = m_rootHash;
        approvalTransactionInfo.m_fsTreeFileSize = m_fsTreeFileSize;
        approvalTransactionInfo.m_metaFilesSize = m_metaFilesSize;
        approvalTransactionInfo.m_driveSize = m_driveSize;

        for (const RpcSingleOpinion& rpcOpinion : m_opinions) {
            drive::SingleOpinion singleOpinion;
            singleOpinion.m_replicatorKey = rpcOpinion.m_replicatorKey;
            singleOpinion.m_uploadLayout = rpcOpinion.getUploadLayout();
            singleOpinion.m_signature = rpcOpinion.m_signature;
            approvalTransactionInfo.m_opinions.push_back(singleOpinion);
        }

        return approvalTransactionInfo;
    }

    RpcModifyApprovalTransactionInfo RpcModifyApprovalTransactionInfo::getRpcModifyApprovalTransactionInfo(const std::array<uint8_t,32>& replicatorPubKey, const drive::ApprovalTransactionInfo& transactionInfo) {
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

    void RpcModifyApprovalTransactionInfo::setOpinions(const std::vector<drive::SingleOpinion>& opinions) {
        for (const drive::SingleOpinion& opinion : opinions) {
            RpcSingleOpinion rpcSingleOpinion;
            rpcSingleOpinion.m_replicatorKey = opinion.m_replicatorKey;
            rpcSingleOpinion.setUploadLayout(opinion.m_uploadLayout);
            rpcSingleOpinion.m_signature = opinion.m_signature.array();
            m_opinions.push_back(rpcSingleOpinion);
        }
    }

    drive::ReplicatorList RpcPrepareDriveTransactionInfo::getReplicators() const {
        return *reinterpret_cast<const drive::ReplicatorList *>(&m_rpcReplicators);
    }

    void RpcDriveInfo::setReplicators(const drive::ReplicatorList& replicators) {
        m_rpcReplicators = *reinterpret_cast<const KeysList *>(&m_rpcReplicators);;
    }

    drive::ReplicatorList RpcDriveInfo::getReplicators() const {
        return *reinterpret_cast<const drive::ReplicatorList *>(&m_rpcReplicators);
    }
}

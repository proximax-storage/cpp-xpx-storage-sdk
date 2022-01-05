///*
//*** Copyright 2021 ProximaX Limited. All rights reserved.
//*** Use of this source code is governed by the Apache 2.0
//*** license that can be found in the LICENSE file.
//*/
//
//#include "drive/RpcTypes.h"
//
//
//namespace sirius::drive::types {
//
//    bool RpcReplicatorInfo::operator==(const RpcReplicatorInfo& replicator) const {
//        return replicator.m_replicatorPubKey == m_replicatorPubKey;
//    }
//
//    bool RpcClientInfo::operator==(const RpcClientInfo& client) const {
//        return client.m_clientPubKey == m_clientPubKey;
//    }
//
//    std::vector<Key> RpcDownloadChannelInfo::getClientsPublicKeys() const {
//        std::vector<Key> clientsPublicKeys(m_clientsPublicKeys.size());
//        std::copy(m_clientsPublicKeys.begin(), m_clientsPublicKeys.end(), clientsPublicKeys.begin());
//
//        return clientsPublicKeys;
//    }
//
//    void RpcDownloadChannelInfo::setClientsPublicKeys(const std::vector<Key>& clientsPublicKeys) {
//        for (const Key& key : clientsPublicKeys) {
//            m_clientsPublicKeys.emplace_back(key.array());
//        }
//    }
//
//    ReplicatorList RpcDownloadChannelInfo::getReplicators() const {
//        ReplicatorList replicators;
//        for (const RpcReplicatorInfo& rpcReplicatorInfo : m_rpcReplicators) {
//            boost::asio::ip::address address = boost::asio::ip::address::from_string(rpcReplicatorInfo.m_replicatorAddress);
//            replicators.emplace_back( drive::ReplicatorInfo{ {
//                                                                     address,
//                                                                     rpcReplicatorInfo.m_replicatorPort
//                                                             },  rpcReplicatorInfo.m_replicatorPubKey } );
//        }
//
//        return replicators;
//    }
//
//    ReplicatorList RpcDataModification::getReplicators() const {
//        ReplicatorList replicators;
//        for (const RpcReplicatorInfo &rpcReplicatorInfo: m_rpcReplicators) {
//            boost::asio::ip::address address = boost::asio::ip::address::from_string(
//                    rpcReplicatorInfo.m_replicatorAddress);
//            replicators.emplace_back(drive::ReplicatorInfo{{
//                                                                   address,
//                                                                   rpcReplicatorInfo.m_replicatorPort
//                                                           }, rpcReplicatorInfo.m_replicatorPubKey});
//        }
//
//        return replicators;
//    }
//
//    void RpcSingleOpinion::setUploadLayout(const std::vector<KeyAndBytes>& uploadLayout)
//    {
//        m_uploadLayout.clear();
//        for (const auto& item: uploadLayout) {
//            m_uploadLayout.push_back( { item.m_key, item.m_uploadedBytes } );
//        }
//    }
//
//    std::vector<KeyAndBytes> RpcSingleOpinion::getUploadLayout() const
//    {
//        std::vector<KeyAndBytes> uploadLayout;
//        for (const auto& item: m_uploadLayout) {
//            uploadLayout.push_back( { item.m_key, item.m_uploadedBytes} );
//        }
//        return uploadLayout;
//    }
//
//    ApprovalTransactionInfo RpcModifyApprovalTransactionInfo::getApprovalTransactionInfo() const {
//        ApprovalTransactionInfo approvalTransactionInfo;
//        approvalTransactionInfo.m_driveKey = m_drivePubKey;
//        approvalTransactionInfo.m_modifyTransactionHash = m_modifyTransactionHash;
//        approvalTransactionInfo.m_rootHash = m_rootHash;
//        approvalTransactionInfo.m_fsTreeFileSize = m_fsTreeFileSize;
//        approvalTransactionInfo.m_metaFilesSize = m_metaFilesSize;
//        approvalTransactionInfo.m_driveSize = m_driveSize;
//
//        for (const RpcSingleOpinion& rpcOpinion : m_opinions) {
//            SingleOpinion singleOpinion;
//            singleOpinion.m_replicatorKey = rpcOpinion.m_replicatorKey;
//            singleOpinion.m_clientUploadBytes = rpcOpinion.m_clientUploadBytes;
//            singleOpinion.m_uploadLayout = rpcOpinion.getUploadLayout();
//            singleOpinion.m_signature = rpcOpinion.m_signature;
//            approvalTransactionInfo.m_opinions.push_back(singleOpinion);
//        }
//
//        return approvalTransactionInfo;
//    }
//
//    RpcModifyApprovalTransactionInfo RpcModifyApprovalTransactionInfo::getRpcModifyApprovalTransactionInfo(const std::array<uint8_t,32>& replicatorPubKey, ApprovalTransactionInfo&& transactionInfo) {
//        types::RpcModifyApprovalTransactionInfo rpcModifyApprovalTransactionInfo;
//        rpcModifyApprovalTransactionInfo.m_drivePubKey = transactionInfo.m_driveKey;
//        rpcModifyApprovalTransactionInfo.m_replicatorPubKey = replicatorPubKey;
//        rpcModifyApprovalTransactionInfo.m_modifyTransactionHash = transactionInfo.m_modifyTransactionHash;
//        rpcModifyApprovalTransactionInfo.m_rootHash = transactionInfo.m_rootHash;
//        rpcModifyApprovalTransactionInfo.m_fsTreeFileSize = transactionInfo.m_fsTreeFileSize;
//        rpcModifyApprovalTransactionInfo.m_metaFilesSize = transactionInfo.m_metaFilesSize;
//        rpcModifyApprovalTransactionInfo.m_driveSize = transactionInfo.m_driveSize;
//        rpcModifyApprovalTransactionInfo.setOpinions(transactionInfo.m_opinions);
//
//        return rpcModifyApprovalTransactionInfo;
//    }
//
//    void RpcModifyApprovalTransactionInfo::setOpinions(const std::vector<SingleOpinion>& opinions) {
//        for (const SingleOpinion& opinion : opinions) {
//            RpcSingleOpinion rpcSingleOpinion;
//            rpcSingleOpinion.m_replicatorKey = opinion.m_replicatorKey;
//            rpcSingleOpinion.m_clientUploadBytes = opinion.m_clientUploadBytes;
//            rpcSingleOpinion.setUploadLayout(opinion.m_uploadLayout);
//            rpcSingleOpinion.m_signature = opinion.m_signature.array();
//            m_opinions.push_back(rpcSingleOpinion);
//        }
//    }
//
//    ReplicatorList RpcPrepareDriveTransactionInfo::getReplicators() const {
//        ReplicatorList replicators;
//        for (const RpcReplicatorInfo& rpcReplicatorInfo : m_rpcReplicators) {
//            boost::asio::ip::address address = boost::asio::ip::address::from_string(rpcReplicatorInfo.m_replicatorAddress);
//            replicators.emplace_back( drive::ReplicatorInfo{
//                    { address, rpcReplicatorInfo.m_replicatorPort },
//                    rpcReplicatorInfo.m_replicatorPubKey } );
//        }
//
//        return replicators;
//    }
//
//    void RpcDriveInfo::setReplicators(const ReplicatorList& replicators) {
//        for (const ReplicatorInfo& replicator : replicators) {
//            RpcReplicatorInfo rpcReplicatorInfo;
//            rpcReplicatorInfo.m_replicatorAddress = replicator.m_endpoint.address().to_string();
//            rpcReplicatorInfo.m_replicatorPort = replicator.m_endpoint.port();
//            rpcReplicatorInfo.m_replicatorPubKey = replicator.m_publicKey.array();
//
//            // TODO: pass as argument
//            rpcReplicatorInfo.m_rpcReplicatorPort = 5510;
//
//            m_rpcReplicators.push_back(rpcReplicatorInfo);
//        }
//    }
//
//    ReplicatorList RpcDriveInfo::getReplicators() const {
//        ReplicatorList replicators;
//        for (const RpcReplicatorInfo& rpcReplicatorInfo : m_rpcReplicators) {
//            boost::asio::ip::address address = boost::asio::ip::address::from_string(rpcReplicatorInfo.m_replicatorAddress);
//            replicators.emplace_back( drive::ReplicatorInfo{
//                    { address, rpcReplicatorInfo.m_replicatorPort },
//                    rpcReplicatorInfo.m_replicatorPubKey } );
//        }
//
//        return replicators;
//    }
//}

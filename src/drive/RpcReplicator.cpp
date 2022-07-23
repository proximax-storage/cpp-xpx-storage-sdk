///*
//*** Copyright 2022 ProximaX Limited. All rights reserved.
//*** Use of this source code is governed by the Apache 2.0
//*** license that can be found in the LICENSE file.
//*/
//
//#pragma once
//
//#include "types.h"
//#include "plugins.h"
//#include "drive/FlatDrive.h"
//#include "drive/Streaming.h"
//#include "crypto/Signer.h"
//
//#include <boost/asio/high_resolution_timer.hpp>
////#include <libtorrent/torrent_handle.hpp>
////#include <boost/asio/ip/tcp.hpp>
//
//
//namespace sirius::drive {
//
////
//// RpcReplicator
////
//class RpcReplicator
//{
//public:
//
//    virtual void start() override {}
//
//    virtual const Key& replicatorKey() const override {}
//
//    // All of the below functions return error string (or empty string)
//
//    // It is called as soon as all drives and channels are added
//    virtual void asyncInitializationFinished() override {}
//
//    // It will be called in 3 cases:
//    // - when received transaction about drive creation (in this case 'actualRootHash' should be empty)
//    // - when storage-extension restarts and initiates the drive (that already was created)
//    // - when replicator joined to existing drive
//    //
//    virtual void asyncAddDrive( Key driveKey, mobj<AddDriveRequest>&& driveRequest ) override {}
//
//    // It is called when the Replicator is removed from the Drive
//    virtual void asyncRemoveDrive( Key driveKey ) override {}
//
//    // It notifies about changes in drive replicator list
//    virtual void asyncSetReplicators( Key driveKey, mobj<std::vector<Key>>&& replicatorKeys ) override {}
//
//    // It notifies about changes in modification shards
//    virtual void asyncSetShardDonator( Key driveKey, mobj<std::vector<Key>>&& replicatorKeys ) override {}
//    virtual void asyncSetShardRecipient( Key driveKey, mobj<std::vector<Key>>&& replicatorKeys ) override {}
//
//    // It notifies about changes in download channel shard
//    virtual void asyncSetChanelShard( mobj<Hash256>&& channelId, mobj<ReplicatorList>&& replicatorKeys ) override {}
//
//    // it starts drive closing
//    virtual void asyncCloseDrive( Key driveKey, Hash256 transactionHash ) override {}
//
//#ifdef __FOR_DEBUGGING__
//    //virtual std::shared_ptr<sirius::drive::FlatDrive> getDrive( const Key& driveKey ) override {}
//#endif
//
//    // it begins modify operation, that will be performed on session thread
//    virtual void        asyncModify( Key driveKey, mobj<ModificationRequest>&&  modifyRequest ) override {}
//
//    virtual void        asyncCancelModify( Key driveKey, Hash256  transactionHash ) override {}
//
//    virtual void        asyncStartDriveVerification( Key driveKey, mobj<VerificationRequest>&& ) override {}
//    virtual void        asyncCancelDriveVerification( Key driveKey ) override {}
//
//    virtual void        asyncStartStream( Key driveKey, mobj<StreamRequest>&& ) override {}
//    virtual void        asyncIncreaseStream( Key driveKey, mobj<StreamIncreaseRequest>&& ) override {}
//    virtual void        asyncFinishStreamTxPublished( Key driveKey, mobj<StreamFinishRequest>&& ) override {}
//
//    // It is called when Replicator is added to the Download Channel Shard
//    virtual void        asyncAddDownloadChannelInfo( Key driveKey, mobj<DownloadRequest>&&  downloadRequest, bool mustBeSynchronized = false ) override {}
//
//    // It is called when the prepaid size of the download channel is increased
//    virtual void        asyncIncreaseDownloadChannelSize( ChannelId channelId, uint64_t size ) override {}
//
//    // It is called when Replicator leaves the Download Channel Shard
//    virtual void        asyncRemoveDownloadChannelInfo( ChannelId channelId ) override {}
//
//    // it will be called when dht message is received
//    virtual void        asyncOnDownloadOpinionReceived( mobj<DownloadApprovalTransactionInfo>&& anOpinion ) override {}
//
//    // Usually, DownloadApprovalTransactions are made once per 24 hours and paid by the Drive Owner
//    // (It initiate opinion exchange, and then publishing of 'DownloadApprovalTransaction')
//    virtual void        asyncInitiateDownloadApprovalTransactionInfo( Hash256 blockHash, Hash256 channelId ) override {}
//
//    // It will
//    virtual void        asyncDownloadApprovalTransactionHasBeenPublished( Hash256 blockHash, Hash256 channelId, bool driveIsClosed = false ) override {}
//
//    virtual void        asyncDownloadApprovalTransactionHasFailedInvalidOpinions( Hash256 eventHash, Hash256 channelId ) override {}
//
//    // It will be called when other replicator calculated rootHash and send his opinion
//    virtual void        asyncOnOpinionReceived( ApprovalTransactionInfo anOpinion ) override {}
//
//    // It will be called after 'MODIFY approval transaction' has been published
//    virtual void        asyncApprovalTransactionHasBeenPublished( mobj<PublishedModificationApprovalTransactionInfo>&& transaction ) override {}
//
//    // It will be called if transaction sent by the Replicator has failed because of invalid Replicators list
//    virtual void        asyncApprovalTransactionHasFailedInvalidOpinions( Key driveKey, Hash256 transactionHash ) override {}
//
//    // It will be called after 'single MODIFY approval transaction' has been published
//    virtual void        asyncSingleApprovalTransactionHasBeenPublished( mobj<PublishedModificationSingleApprovalTransactionInfo>&& transaction ) override {}
//
//    // It will be called after 'VERIFY approval transaction' has been published
//    virtual void        asyncVerifyApprovalTransactionHasBeenPublished( PublishedVerificationApprovalTransactionInfo info ) override {}
//
//    // It will be called if transaction sent by the Replicator has failed because of invalid Replicators list
//    virtual void        asyncVerifyApprovalTransactionHasFailedInvalidOpinions( Key driveKey, Hash256 verificationId ) override {}
//
//    // Max difference between requested data and signed receipt
//    //
//    virtual uint64_t    receiptLimit() const override {}
//    virtual void        setReceiptLimit( uint64_t newLimitInBytes ) override {}
//
//    // Timeout delays
//    //
//    virtual void        setDownloadApprovalTransactionTimerDelay( int milliseconds ) override {}
//    virtual void        setModifyApprovalTransactionTimerDelay( int milliseconds ) override {}
//    virtual int         getModifyApprovalTransactionTimerDelay() override {}
//    virtual void        setVerifyCodeTimerDelay( int milliseconds ) override {}
//    virtual int         getVerifyCodeTimerDelay() override {}
//    virtual void        setVerifyApprovalTransactionTimerDelay( int milliseconds ) override {}
//    virtual int         getVerifyApprovalTransactionTimerDelay() override {}
//    virtual void        setSessionSettings(const lt::settings_pack&, bool localNodes) override {}
//
//    // Functions for debugging
//    //
//    virtual Hash256     dbgGetRootHash( const DriveKey& driveKey ) override {}
//    virtual void        dbgPrintDriveStatus( const Key& driveKey ) override {}
//    virtual void        dbgPrintTrafficDistribution( std::array<uint8_t,32>  transactionHash ) override {}
//    virtual const char* dbgReplicatorName() const override {}
//    virtual std::shared_ptr<sirius::drive::FlatDrive> dbgGetDrive( const std::array<uint8_t,32>& driveKey ) override {}
//    virtual const Key&  dbgReplicatorKey() const override {}
//
//    virtual void        dbgAsyncDownloadToSandbox( Key driveKey, InfoHash, std::function<void()> endNotifyer ) override {}
//
//};
//
//PLUGIN_API std::shared_ptr<Replicator> createRpcReplicator(
//                                               const crypto::KeyPair&,
//                                               std::string&&  address,
//                                               std::string&&  port,
//                                               std::string&&  storageDirectory,
//                                               std::string&&  sandboxDirectory,
//                                               const std::vector<ReplicatorInfo>&  bootstraps,
//                                               bool           useTcpSocket, // use TCP socket (instead of uTP)
//                                               ReplicatorEventHandler&,
//                                               DbgReplicatorEventHandler*  dbgEventHandler = nullptr,
//                                               const char*    dbgReplicatorName = ""
//)
//{
//
//}
//
//}
//

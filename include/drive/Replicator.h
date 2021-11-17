/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include "types.h"
#include "plugins.h"
#include "drive/FlatDrive.h"
#include "crypto/Signer.h"
#include "Session.h"


namespace sirius::drive {

// It is used for calculation of total data size, downloaded by 'client'
struct ReplicatorUploadInfo
{
    // It is the size uploaded by another replicator
    uint64_t m_uploadedSize = 0;
};
using ReplicatorUploadMap = std::map<std::array<uint8_t,32>,ReplicatorUploadInfo>;

struct DownloadChannelInfo
{
    bool                    m_isModifyTx;
    
    uint64_t                m_prepaidDownloadSize;
    uint64_t                m_requestedSize = 0;
    uint64_t                m_uploadedSize = 0;
    std::array<uint8_t,32>  m_driveKey;
    ReplicatorList          m_replicatorsList;      //todo must be synchronized with drive.m_replicatorList (in higher versions?)
    ReplicatorUploadMap     m_replicatorUploadMap;
    std::vector<std::array<uint8_t, 32>> m_clients; //todo
    
    // it is used when drive is closing
    bool                    m_isClosed = false;
};

// key is a channel hash
using ChannelMap         = std::map<std::array<uint8_t,32>, DownloadChannelInfo>;

struct DownloadOpinionMapValue
{
    DownloadApprovalTransactionInfo                     m_info;
    bool                                                m_approveTransactionSent = false;
    bool                                                m_approveTransactionReceived = false;
    std::optional<boost::asio::high_resolution_timer>   m_timer = {};
    boost::posix_time::ptime                            m_creationTime = boost::posix_time::microsec_clock::universal_time();
};

// DownloadOpinionMap (key is a blockHash value)
using DownloadOpinionMap = std::map<std::array<uint8_t,32>, DownloadOpinionMapValue>;

// It is used for mutual calculation of the replicators, when they download 'modify data'
// (Note. Replicators could receive 'modify data' from client and from replicators, that already receives some piece)
struct ModifyTrafficInfo
{
    // It is the size received from another replicator or client
    uint64_t m_receivedSize = 0;
    
    // It is the size sent to another replicator
    uint64_t m_requestedSize = 0;
    uint64_t m_sentSize = 0;
};

// The key is a transaction hash
using ModifyTrafficMap = std::map<std::array<uint8_t,32>,ModifyTrafficInfo>;

struct ModifyDriveInfo
{
    std::array<uint8_t,32>  m_driveKey;
    uint64_t                m_maxDataSize;
    ReplicatorList          m_replicatorsList;
    ModifyTrafficMap        m_modifyTrafficMap;
    uint64_t                m_totalReceivedSize = 0;
};

// The key is a transaction hash
using ModifyDriveMap    = std::map<std::array<uint8_t,32>, ModifyDriveInfo>;


//
// Replicator
//
class Replicator
{
public:

    virtual ~Replicator() = default;

    virtual void start() = 0;
    
    virtual const Key& replicatorKey() const = 0;

    virtual const crypto::KeyPair& keyPair() const = 0;

    // All of the below functions return error string (or empty string)
    
    virtual std::string addDrive( const Key& driveKey,  DriveRequest&& driveRequest) = 0;

    // it starts drive closing
    virtual std::string removeDrive( const Key& driveKey, const Hash256& transactionHash ) = 0;

#ifdef __FOR_DEBUGGING__
    //virtual std::shared_ptr<sirius::drive::FlatDrive> getDrive( const Key& driveKey ) = 0;
#endif
    
    // it begins modify operation, that will be performed on session thread
    virtual std::string modify( const Key&          driveKey,
                                ModifyRequest&&     modifyRequest ) = 0;

    virtual std::string cancelModify( const Key&        driveKey,
                                      const Hash256&    transactionHash ) = 0;

    virtual Hash256     getRootHash( const Key& driveKey ) = 0;
    
    virtual ModifyDriveInfo getMyDownloadOpinion( const Hash256& transactionHash ) = 0;

    virtual std::string loadTorrent( const Key& driveKey, const InfoHash& infoHash ) = 0;

    // 'replicatorsList' is used to notify other replictors
    // (it does not contain its own endpoint)
    virtual void        addDownloadChannelInfo( const Key&          driveKey,
                                                DownloadRequest&&   downloadRequest ) = 0;

    //
    virtual void        removeDownloadChannelInfo( const std::array<uint8_t,32>& channelId ) = 0;

    //
    virtual void        onDownloadOpinionReceived( DownloadApprovalTransactionInfo&& anOpinion ) = 0;
    
    // Usually, DownloadApprovalTransactions are made once per 24 hours and paid by the Drive Owner
    // (It initiate opinion exchange, and then publishing of 'DownloadApprovalTransaction')
    virtual void        prepareDownloadApprovalTransactionInfo( const Hash256& blockHash, const Hash256& channelId ) = 0;

    // It will clear opinion map
    virtual void        onDownloadApprovalTransactionHasBeenPublished( const Hash256& blockHash, const Hash256& channelId, bool driveIsClosed = false ) = 0;

    // It will be called when other replicator calculated rootHash and send his opinion
    virtual void        onOpinionReceived( const ApprovalTransactionInfo& anOpinion ) = 0;
    
    // It will be called after 'MODIFY approval transaction' has been published
    virtual void        onApprovalTransactionHasBeenPublished( const ApprovalTransactionInfo& transaction ) = 0;

    // It will be called after 'single MODIFY approval transaction' has been published
    virtual void        onSingleApprovalTransactionHasBeenPublished( const ApprovalTransactionInfo& transaction ) = 0;

    // It continues drive closing (initiates DownloadApprovalTransaction and then removes drive)
    virtual void        closeDriveChannels( const Hash256& blockHash, FlatDrive& drive ) = 0;
    
    // It will be used while drive closing
    virtual void        removeModifyDriveInfo( const std::array<uint8_t,32>& modifyTransactionHash ) = 0;

    
    // TODO:
    // They will be called after 'cancel modify transaction' has been published
//    virtual void        onTransactionCanceled( ApprovalTransactionInfo&& transaction ) = 0;
//    virtual void        onTransactionCanceledByClient( ApprovalTransactionInfo&& transaction ) = 0;

    // Max difference between requested data and signed receipt
    virtual uint64_t    receiptLimit() const = 0;

    virtual void        setReceiptLimit( uint64_t newLimitInBytes ) = 0;

    virtual void        setDownloadApprovalTransactionTimerDelay( int milliseconds ) = 0;
    virtual void        setModifyApprovalTransactionTimerDelay( int milliseconds ) = 0;
    virtual int         getModifyApprovalTransactionTimerDelay() = 0;
    virtual void        setSessionSettings(const lt::settings_pack&, bool localNodes) = 0;

    
    // Message exchange
    virtual void        sendMessage( const std::string& query, boost::asio::ip::tcp::endpoint, const std::string& ) = 0;
    
    // It was moveed into ;session_delegate'
    //virtual void        onMessageReceived( const std::string& query, const std::string& ) = 0;
    

    virtual void        printDriveStatus( const Key& driveKey ) = 0;
    
    virtual void        printTrafficDistribution( const std::array<uint8_t,32>&  transactionHash ) = 0;
    
    virtual ReplicatorEventHandler& eventHandler() = 0;
    
    virtual const char* dbgReplicatorName() const = 0;
};

PLUGIN_API std::shared_ptr<Replicator> createDefaultReplicator(
                                               const crypto::KeyPair&,
                                               std::string&&  address,
                                               std::string&&  port,
                                               std::string&&  storageDirectory,
                                               std::string&&  sandboxDirectory,
                                               bool           useTcpSocket, // use TCP socket (instead of uTP)
                                               ReplicatorEventHandler&,
                                               DbgReplicatorEventHandler*  dbgEventHandler = nullptr,
                                               const char*    dbgReplicatorName = ""
);

}

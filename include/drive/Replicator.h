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
    template <class Archive> void serialize( Archive & arch )
    {
        arch(m_uploadedSize);
    }
};
using ReplicatorUploadMap = std::map<std::array<uint8_t,32>,ReplicatorUploadInfo>;

struct DownloadOpinionMapValue
{
    std::array<uint8_t,32>                              m_eventHash;
    std::array<uint8_t,32>                              m_downloadChannelId;
    std::map<std::array<uint8_t,32>, DownloadOpinion>   m_opinions;
    bool                                                m_modifyApproveTransactionSent = false;
    bool                                                m_approveTransactionReceived = false;
    std::optional<boost::asio::high_resolution_timer>   m_timer = {};
    std::optional<boost::asio::high_resolution_timer>   m_opinionShareTimer = {};
    boost::posix_time::ptime                            m_creationTime = boost::posix_time::microsec_clock::universal_time();

    DownloadOpinionMapValue() {}
    
    DownloadOpinionMapValue( const std::array<uint8_t,32>                       eventHash,
                             const std::array<uint8_t,32>&                      downloadChannelId,
                             std::map<std::array<uint8_t,32>, DownloadOpinion>  opinions )
    :   m_eventHash(eventHash),
        m_downloadChannelId(downloadChannelId),
        m_opinions(opinions)
    {}

    DownloadOpinionMapValue( const DownloadOpinionMapValue& newValue )
    {
        m_eventHash                     = newValue.m_eventHash;
        m_downloadChannelId             = newValue.m_downloadChannelId;
        m_opinions                      = newValue.m_opinions;
        m_modifyApproveTransactionSent  = newValue.m_modifyApproveTransactionSent;
        m_approveTransactionReceived    = newValue.m_approveTransactionReceived;
    }
    
    DownloadOpinionMapValue& operator=( const DownloadOpinionMapValue& newValue )&
    {
        m_eventHash                     = newValue.m_eventHash;
        m_downloadChannelId             = newValue.m_downloadChannelId;
        m_opinions                      = newValue.m_opinions;
        m_modifyApproveTransactionSent  = newValue.m_modifyApproveTransactionSent;
        m_approveTransactionReceived    = newValue.m_approveTransactionReceived;
        return *this;
    }
    
    template <class Archive> void serialize( Archive & arch )
    {
        arch(m_eventHash);
        arch(m_downloadChannelId);
        arch(m_opinions);
        arch(m_modifyApproveTransactionSent);
        arch(m_approveTransactionReceived);
        //TODO ??m_creationTime
    }
};

// DownloadOpinionMap (key is a blockHash value)
using DownloadOpinionMap = std::map<std::array<uint8_t,32>, DownloadOpinionMapValue>;

struct DownloadChannelInfo
{
    bool m_isModifyTx;

    uint64_t m_prepaidDownloadSize;
    uint64_t m_totalReceiptsSize = 0;
    uint64_t m_requestedSize = 0;
    uint64_t m_uploadedSize = 0;
    std::array<uint8_t, 32> m_driveKey;
    ReplicatorList m_replicatorsList2;      //todo must be synchronized with drive.m_replicatorList (in higher versions?)
    ReplicatorUploadMap m_replicatorUploadMap;
    std::vector<std::array<uint8_t, 32>> m_clients; //todo
    DownloadOpinionMap m_downloadOpinionMap;

    // it is used when drive is closing
    bool m_isClosed = false;
    
    template <class Archive> void serialize( Archive & arch )
    {
        arch( m_isModifyTx );
        arch( m_prepaidDownloadSize );
        arch( m_uploadedSize );
        arch( m_driveKey );
        arch(m_totalReceiptsSize );
        arch( m_replicatorUploadMap );
        arch( m_clients );
        arch( m_downloadOpinionMap );
    }
};

// key is a channel hash
using ChannelMap         = std::map<std::array<uint8_t,32>, DownloadChannelInfo>;

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

struct ExternalEndpointRequest {

    std::array<uint8_t, 32> m_requestTo;
    std::array<uint8_t, 32> m_challenge;

    template<class Archive>
    void serialize(Archive &arch)
    {
        arch(m_requestTo);
        arch(m_challenge);
    }
};

struct ExternalEndpointResponse {

    std::array<uint8_t, 32> m_requestTo;
    std::array<uint8_t, 32> m_challenge;
    std::array<uint8_t, sizeof(boost::asio::ip::tcp::endpoint)> m_endpoint;
    Signature m_signature;

    void Sign( const crypto::KeyPair& keyPair )
    {
        crypto::Sign(keyPair,
                     {
            utils::RawBuffer{ m_challenge },
            utils::RawBuffer{ m_endpoint }
            },
            m_signature);
    }

    bool Verify() const
    {
        return crypto::Verify(m_requestTo,
                       {
                               utils::RawBuffer{m_challenge},
                               utils::RawBuffer{m_endpoint}
                       },
                       m_signature);
    }

    template<class Archive>
    void serialize(Archive &arch)
    {
        arch(m_requestTo);
        arch(m_challenge);
        arch(m_endpoint);
        arch(cereal::binary_data(m_signature.data(), m_signature.size()));
    }
};

struct DhtHandshake
{
    std::array<uint8_t, 32> m_fromPublicKey;
    std::array<uint8_t, 32> m_toPublicKey;
    std::array<uint8_t, sizeof(boost::asio::ip::tcp::endpoint)> m_endpoint;
    Signature m_signature;

    void Sign( const crypto::KeyPair& keyPair )
    {
        crypto::Sign(keyPair,
                     {
                             utils::RawBuffer{ m_toPublicKey },
                             utils::RawBuffer{ m_endpoint }
                     },
                     m_signature);
    }

    bool Verify() const
    {
        return crypto::Verify(m_fromPublicKey,
                       {
                               utils::RawBuffer{m_toPublicKey},
                               utils::RawBuffer{m_endpoint}
                       },
                       m_signature);
    }


    template<class Archive>
    void serialize(Archive &arch)
    {
        arch(m_fromPublicKey);
        arch(m_toPublicKey);
        arch(m_endpoint);
        arch(cereal::binary_data(m_signature.data(), m_signature.size()));
    }
};

struct EndpointInformation
{
    std::optional<boost::asio::ip::tcp::endpoint> m_endpoint;
    std::optional<boost::asio::high_resolution_timer> m_timer;
};

//
// Replicator
//
class Replicator
{
public:

    virtual ~Replicator()
    {
    }

    virtual void start() = 0;
    
    virtual const Key& replicatorKey() const = 0;

    virtual const crypto::KeyPair& keyPair() const = 0;

    // All of the below functions return error string (or empty string)

    // It is called as soon as all drives and channels are added
    virtual void asyncInitializationFinished() = 0;

    // It will be called in 3 cases:
    // - when received transaction about drive creation (in this case 'actualRootHash' should be empty)
    // - when storage-extension restarts and initiates the drive (that already was created)
    // - when replicator joined to existing drive
    //
    virtual void asyncAddDrive( Key driveKey, AddDriveRequest driveRequest ) = 0;

    // It is called when the Replicator is removed from the Drive
    virtual void asyncRemoveDrive( Key driveKey ) = 0;

    // It is called when the Replicator becomes the member of another Replicator's shard
    virtual void asyncAddUploadShard( Key driveKey, Key shardOwner ) = 0;

    // It is called when the Replicator is removed form the members of another Replicator's shard
    virtual void asyncRemoveUploadShard( Key driveKey, Key shardOwner ) = 0;

    // It is called when a new Replicator is added to this Replicator's shard,
    // and so we are allowed to download from the Replicator
    virtual void asyncAddToMyUploadShard( Key driveKey, Key replicator ) = 0;

    // It is called when a Replicator is removed from this Replicator's shard,
    // and so we are not allowed to download from the Replicator
    virtual void asyncRemoveFromMyUploadShard( Key driveKey, Key replicator ) = 0;

    // it starts drive closing
    virtual void asyncCloseDrive( Key driveKey, Hash256 transactionHash ) = 0;

#ifdef __FOR_DEBUGGING__
    //virtual std::shared_ptr<sirius::drive::FlatDrive> getDrive( const Key& driveKey ) = 0;
#endif
    
    // it begins modify operation, that will be performed on session thread
    virtual void        asyncModify( Key driveKey, ModifyRequest  modifyRequest ) = 0;

    virtual void        asyncCancelModify( Key driveKey, Hash256  transactionHash ) = 0;
    
    virtual void        asyncStartDriveVerification( Key driveKey, mobj<VerificationRequest>&& ) = 0;
    virtual void        asyncCancelDriveVerification( Key driveKey, mobj<Hash256>&& tx ) = 0;

    // It is called when Replicator is added to the Download Channel Shard
    virtual void        asyncAddDownloadChannelInfo( Key driveKey, DownloadRequest&&  downloadRequest ) = 0;

    // It is called when Replicator leaves the Download Channel Shard
    virtual void        asyncRemoveDownloadChannelInfo( Key driveKey, Key channelId ) = 0;

    // it will be called when dht message is received
    virtual void        asyncOnDownloadOpinionReceived( DownloadApprovalTransactionInfo anOpinion ) = 0;
    
    // Usually, DownloadApprovalTransactions are made once per 24 hours and paid by the Drive Owner
    // (It initiate opinion exchange, and then publishing of 'DownloadApprovalTransaction')
    virtual void        asyncInitiateDownloadApprovalTransactionInfo( Hash256 blockHash, Hash256 channelId ) = 0;

    // It will
    virtual void        asyncDownloadApprovalTransactionHasBeenPublished( Hash256 blockHash, Hash256 channelId, bool driveIsClosed = false ) = 0;

    virtual void        asyncDownloadApprovalTransactionHasFailedInvalidOpinions( Hash256 eventHash, Hash256 channelId ) = 0;

    // It will be called when other replicator calculated rootHash and send his opinion
    virtual void        asyncOnOpinionReceived( ApprovalTransactionInfo anOpinion ) = 0;

    // It will be called when a new opinion should be verified
    virtual void        processDownloadOpinion( const DownloadApprovalTransactionInfo& anOpinion ) = 0;

    // It will be called when a new opinion should be verified
    virtual void        processOpinion( const ApprovalTransactionInfo& anOpinion ) = 0;
    
    // It will be called after 'MODIFY approval transaction' has been published
    virtual void        asyncApprovalTransactionHasBeenPublished( PublishedModificationApprovalTransactionInfo transaction ) = 0;

    // It will be called if transaction sent by the Replicator has failed because of invalid Replicators list
    virtual void        asyncApprovalTransactionHasFailedInvalidSignatures( Key driveKey, Hash256 transactionHash ) = 0;

    // It will be called after 'single MODIFY approval transaction' has been published
    virtual void        asyncSingleApprovalTransactionHasBeenPublished( PublishedModificationSingleApprovalTransactionInfo transaction ) = 0;

    // It will be called after 'VERIFY approval transaction' has been published
    virtual void        asyncVerifyApprovalTransactionHasBeenPublished( PublishedVerificationApprovalTransactionInfo info ) = 0;

    // It will be called if transaction sent by the Replicator has failed because of invalid Replicators list
    virtual void        asyncVerifyApprovalTransactionHasFailedInvalidOpinions( Key driveKey, Hash256 verificationId ) = 0;

    // It continues drive closing (initiates DownloadApprovalTransaction and then removes drive)
    virtual void        closeDriveChannels( const Hash256& blockHash, FlatDrive& drive ) = 0;
    
    virtual void        finishDriveClosure( const Key& driveKey ) = 0;

    // It will be used while drive closing
    virtual void        removeModifyDriveInfo( const std::array<uint8_t,32>& modifyTransactionHash ) = 0;

    
    // TODO:
    // They will be called after 'cancel modify transaction' has been published
//    virtual void        onTransactionCanceled( ApprovalTransactionInfo&& transaction ) = 0;
//    virtual void        onTransactionCanceledByClient( ApprovalTransactionInfo&& transaction ) = 0;

    // Max difference between requested data and signed receipt
    //
    virtual uint64_t    receiptLimit() const = 0;
    virtual void        setReceiptLimit( uint64_t newLimitInBytes ) = 0;

    // Timeout delays
    //
    virtual void        setDownloadApprovalTransactionTimerDelay( int milliseconds ) = 0;
    virtual void        setModifyApprovalTransactionTimerDelay( int milliseconds ) = 0;
    virtual int         getModifyApprovalTransactionTimerDelay() = 0;
    virtual void        setVerifyCodeTimerDelay( int milliseconds ) = 0;
    virtual int         getVerifyCodeTimerDelay() = 0;
    virtual void        setVerifyApprovalTransactionTimerDelay( int milliseconds ) = 0;
    virtual int         getVerifyApprovalTransactionTimerDelay() = 0;
    virtual void        setSessionSettings(const lt::settings_pack&, bool localNodes) = 0;

    
    // Message exchange
    virtual void        sendMessage( const std::string& query,
                                     const std::array<uint8_t,32>&  replicatorKey,
                                     const std::vector<uint8_t>& ) = 0;
    
    virtual void        sendMessage( const std::string&             query,
                                     const std::array<uint8_t,32>&  replicatorKey,
                                     const std::string&             message ) = 0;

    // It was moveed into ;session_delegate'
    //virtual void        onMessageReceived( const std::string& query, const std::string& ) = 0;
    
    virtual ModifyDriveInfo getMyDownloadOpinion( const Hash256& transactionHash ) const = 0;

    //virtual std::string loadTorrent( const Key& driveKey, const InfoHash& infoHash ) = 0;

    // Functions for debugging
    //
    virtual Hash256     dbgGetRootHash( const Key& driveKey ) = 0;
    virtual void        dbgPrintDriveStatus( const Key& driveKey ) = 0;
    virtual void        dbgPrintTrafficDistribution( const std::array<uint8_t,32>&  transactionHash ) = 0;
    virtual const char* dbgReplicatorName() const = 0;
    virtual std::shared_ptr<sirius::drive::FlatDrive> dbgGetDrive( const std::array<uint8_t,32>& driveKey ) = 0;
};

PLUGIN_API std::shared_ptr<Replicator> createDefaultReplicator(
                                               const crypto::KeyPair&,
                                               std::string&&  address,
                                               std::string&&  port,
                                               std::string&&  storageDirectory,
                                               std::string&&  sandboxDirectory,
                                               const std::vector<ReplicatorInfo>&  bootstraps,
                                               bool           useTcpSocket, // use TCP socket (instead of uTP)
                                               ReplicatorEventHandler&,
                                               DbgReplicatorEventHandler*  dbgEventHandler = nullptr,
                                               const char*    dbgReplicatorName = ""
);

}

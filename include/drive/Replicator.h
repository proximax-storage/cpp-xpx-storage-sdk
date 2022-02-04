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

namespace sirius::drive {

// It is used for calculation of total data size, downloaded by 'client'
struct ReplicatorUploadInfo
{
public:
    // It is the size uploaded (to client) by another replicator
    //
    std::map<ClientKey,uint64_t> m_clientMap;

    uint64_t uploadedSize() const
    {
        uint64_t uploadedSize = 0;
        for( auto& cell: m_clientMap )
        {
            uploadedSize += cell.second;
        }
        return uploadedSize;
    }
    
    uint64_t uploadedSize( const ClientKey& clientKey )
    {
        return m_clientMap[clientKey];
    }
    
    void acceptReceipt( const ClientKey& clientKey, uint64_t uploadedSize )
    {
        auto it = m_clientMap.lower_bound( clientKey );
        __ASSERT( it != m_clientMap.end() )
        __ASSERT( it->second < uploadedSize )
        it->second = uploadedSize;
    }
    
    template <class Archive> void serialize( Archive & arch )
    {
        arch(m_clientMap);
    }
};
using ReplicatorUploadMap = std::map<ReplicatorKey,ReplicatorUploadInfo>;

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
        //(???) are they needed?
        arch(m_modifyApproveTransactionSent);
        arch(m_approveTransactionReceived);
    }
};

// DownloadOpinionMap (key is a blockHash value)
using DownloadOpinionMap = std::map<std::array<uint8_t,32>, DownloadOpinionMapValue>;

struct RcptMessage : public std::vector<uint8_t>
{
    using Sign = std::array<uint8_t,64>;
    
    RcptMessage() = default;
    RcptMessage( const RcptMessage& ) = default;
    RcptMessage& operator=( const RcptMessage& ) = default;
    RcptMessage( RcptMessage&& ) = default;
    RcptMessage& operator=( RcptMessage&& ) = default;
    
    RcptMessage( const char* data, size_t dataSize ) : std::vector<uint8_t>( (const uint8_t*)data, ((const uint8_t*)data)+dataSize ) {}

    RcptMessage( const ChannelId&     dnChannelId,
                 const ClientKey&     clientKey,
                 const ReplicatorKey& replicatorKey,
                 uint64_t             downloadedSize,
                 const Sign&          signature )
    {
        reserve( 96+64 );
        insert( end(), dnChannelId.begin(),         dnChannelId.end() );
        insert( end(), clientKey.begin(),           clientKey.end() );
        insert( end(), replicatorKey.begin(),       replicatorKey.end() );
        insert( end(), (uint8_t*)&downloadedSize,   ((uint8_t*)&downloadedSize)+8 );
        insert( end(), signature.begin(),           signature.end() );
    }
    
    bool isValid() const { return size() == sizeof(ChannelId)+sizeof(ClientKey)+sizeof(ReplicatorKey)+8+sizeof(Sign); }

    const ChannelId&      channelId()      const { return *reinterpret_cast<const ChannelId*>(     &this->at(0) );   }
    const ClientKey&      clientKey()      const { return *reinterpret_cast<const ClientKey*>(     &this->at(32) );  }
    const ReplicatorKey&  replicatorKey()  const { return *reinterpret_cast<const ReplicatorKey*>( &this->at(64) );  }
    uint64_t              downloadedSize() const { return *reinterpret_cast<const uint64_t*>(      &this->at(96) );  }
    const uint64_t*       downloadedSizePtr() const { return (const uint64_t*)(    &this->at(96) );  }

    const Sign&           signature()      const { return *reinterpret_cast<const Sign*>(          &this->at(104) ); }
};

using ClientReceipts = std::map<ReplicatorKey,RcptMessage>;

struct DownloadChannelInfo
{
    struct ClientSizes
    {
        uint64_t m_requestedSize = 0; // is it needed?
        uint64_t m_uploadedSize = 0;

        template <class Archive> void serialize( Archive & arch )
        {
            arch( m_requestedSize );
            arch( m_uploadedSize );
        }
    };

    bool     m_isModifyTx;
    bool     m_isSyncronizing;

    uint64_t m_prepaidDownloadSize;
    uint64_t m_totalReceiptsSize = 0;

    std::map<ClientKey,ClientSizes> m_dnClientMap;

    std::array<uint8_t,32>  m_driveKey;
    ReplicatorList          m_dnReplicatorShard;
    ReplicatorUploadMap     m_replicatorUploadMap;
    
    DownloadOpinionMap      m_downloadOpinionMap;
    
    std::map<ClientKey,ClientReceipts> m_clientReceiptMap = {};

    // it is used when drive is closing
    bool m_isClosed = false;

    // for saving/loading channel map before/after shutdown
    template <class Archive> void serialize( Archive & arch )
    {
        arch( m_isModifyTx );
        arch( m_prepaidDownloadSize );
        arch( m_driveKey );
        arch(m_totalReceiptsSize );
        arch( m_replicatorUploadMap );
        arch( m_dnClientMap );
        arch( m_downloadOpinionMap );
        arch( m_clientReceiptMap );
    }
};

// key is a channel hash
using ChannelMap         = std::map<ChannelId, DownloadChannelInfo>;

// It is used for mutual calculation of the replicators, when they download 'modify data'
// (Note. Replicators could receive 'modify data' from client and from replicators, that already receives some piece)
struct ModifyTraffic
{
    // It is the size received from another replicator or client
    uint64_t m_receivedSize = 0;
    
    // It is the size sent to another replicator
    uint64_t m_requestedSize = 0;
    uint64_t m_sentSize = 0;
};

// The key is a transaction hash
using ModifyTrafficMap = std::map<std::array<uint8_t,32>,ModifyTraffic>;

struct ModifyTrafficInfo
{
    std::array<uint8_t,32>  m_driveKey;
    uint64_t                m_maxDataSize;
    ModifyTrafficMap        m_modifyTrafficMap;
    uint64_t                m_totalReceivedSize = 0;
};

// The key is a transaction hash
using ModifyDriveMap    = std::map<std::array<uint8_t,32>, ModifyTrafficInfo>;

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

    // It notifies about changes in drive replicator list
    virtual void asyncReplicatorAdded( Key driveKey, mobj<Key>&& replicatorKey ) = 0;
    virtual void asyncReplicatorRemoved( Key driveKey, mobj<Key>&& replicatorKey ) = 0;

    // It notifyes about changes in modification shards
    virtual void asyncAddShardDonator( Key driveKey, mobj<Key>&& replicatorKey ) = 0;
    virtual void asyncRemoveShardDonator( Key driveKey, mobj<Key>&& replicatorKey ) = 0;
    virtual void asyncAddShardRecipient( Key driveKey, mobj<Key>&& replicatorKey ) = 0;
    virtual void asyncRemoveShardRecipient( Key driveKey, mobj<Key>&& replicatorKey ) = 0;

    // It notifyes about changes in download channel shard
    virtual void asyncAddToChanelShard( mobj<Hash256>&& channelId, mobj<Key>&& replicatorKey ) = 0;
    virtual void asyncRemoveFromChanelShard( mobj<Hash256>&& channelId, mobj<Key>&& replicatorKey ) = 0;

    // it starts drive closing
    virtual void asyncCloseDrive( Key driveKey, Hash256 transactionHash ) = 0;

#ifdef __FOR_DEBUGGING__
    //virtual std::shared_ptr<sirius::drive::FlatDrive> getDrive( const Key& driveKey ) = 0;
#endif
    
    // it begins modify operation, that will be performed on session thread
    virtual void        asyncModify( Key driveKey, ModificationRequest  modifyRequest ) = 0;

    virtual void        asyncCancelModify( Key driveKey, Hash256  transactionHash ) = 0;
    
    virtual void        asyncStartDriveVerification( Key driveKey, mobj<VerificationRequest>&& ) = 0;
    virtual void        asyncCancelDriveVerification( Key driveKey, mobj<Hash256>&& tx ) = 0;

    // It is called when Replicator is added to the Download Channel Shard
    virtual void        asyncAddDownloadChannelInfo( Key driveKey, DownloadRequest&&  downloadRequest, bool mustBeSyncronized = false ) = 0;

    // It is called when Replicator leaves the Download Channel Shard
    virtual void        asyncRemoveDownloadChannelInfo( Key driveKey, Key channelId ) = 0;

    // it will be called when dht message is received
    virtual void        asyncOnDownloadOpinionReceived( mobj<DownloadApprovalTransactionInfo>&& anOpinion ) = 0;
    
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
    virtual void        closeDriveChannels( const mobj<Hash256>& blockHash, const Key& driveKey ) = 0;
    
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

    // will be called from Sesion
    virtual void        onMessageReceived( const std::string& query, const std::string&, const boost::asio::ip::udp::endpoint& source ) = 0;
    virtual bool        createSyncOpinion( const std::array<uint8_t,32>& driveKey,
                                           const std::array<uint8_t,32>& channelId,
                                           DownloadOpinion& outOpinion ) = 0;

    virtual void        onSyncDnOpinionReceived( const std::string& retString ) = 0;

    // will be called from Sesion
    // when it receives message from another replicator
    // (must be implemented by DownloadLimiter)
    virtual bool acceptReceiptFromAnotherReplicator( RcptMessage&& message ) = 0;

    virtual ModifyTrafficInfo getMyDownloadOpinion( const Hash256& transactionHash ) const = 0;

    virtual DownloadChannelInfo* getDownloadChannelInfo( const std::array<uint8_t,32>& driveKey, const std::array<uint8_t,32>& downloadChannelHash ) = 0;

    //virtual std::string loadTorrent( const Key& driveKey, const InfoHash& infoHash ) = 0;

    // Functions for debugging
    //
    virtual Hash256     dbgGetRootHash( const DriveKey& driveKey ) = 0;
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

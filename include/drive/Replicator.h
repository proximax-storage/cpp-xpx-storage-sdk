/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include "types.h"
#include "plugins.h"
#include "drive/FlatDrive.h"
#include "drive/Timer.h"
#include "drive/Streaming.h"
#include "drive/ModificationsExecutor.h"
#include "crypto/Signer.h"
#include "drive/RcptMessage.h"
#include "Kademlia.h"


#include <boost/asio/high_resolution_timer.hpp>
//#include <libtorrent/torrent_handle.hpp>
//#include <boost/asio/ip/tcp.hpp>


namespace sirius::drive {

// It is used for calculation of total data size, requested by 'client'
struct ReplicatorUploadRequestInfo
{
    struct ReceiptSizes
    {
        uint64_t m_acceptedSize = 0;
        uint64_t m_notAcceptedSize = 0;

        template <class Archive> void serialize( Archive & arch )
        {
            arch(m_acceptedSize);
            arch(m_notAcceptedSize);
        }
    };
    
    // It is the size that will be uploaded (to client) by my or another replicator
    //
    std::map<ClientKey,ReceiptSizes> m_clientMap;

public:

    uint64_t lastAcceptedReceiptSize( const ClientKey& clientKey )
    {
        return m_clientMap[clientKey].m_acceptedSize;
    }
    
    uint64_t maxReceiptSize( const ClientKey& clientKey )
    {
        if ( m_clientMap[clientKey].m_acceptedSize < m_clientMap[clientKey].m_notAcceptedSize )
        {
            return m_clientMap[clientKey].m_notAcceptedSize;
        }
        return m_clientMap[clientKey].m_acceptedSize;
    }
    
    uint64_t totalAcceptedReceiptSize() const
    {
        uint64_t receiptSize = 0;
        for( auto& cell: m_clientMap )
        {
            receiptSize += cell.second.m_acceptedSize;
        }
        return receiptSize;
    }
    
    void saveReceiptSize( const ClientKey& clientKey, uint64_t tobeUploadedSize )
    {
        auto it = m_clientMap.find( clientKey );
        _SIRIUS_ASSERT( it != m_clientMap.end() )
        _SIRIUS_ASSERT( it->second.m_acceptedSize <= tobeUploadedSize )
        it->second.m_acceptedSize = tobeUploadedSize;
    }
    
    void saveNotAcceptedReceiptSize( const ClientKey& clientKey, uint64_t size )
    {
        auto it = m_clientMap.find( clientKey );
        _SIRIUS_ASSERT( it != m_clientMap.end() )
        if ( it->second.m_notAcceptedSize < size )
        {
            it->second.m_notAcceptedSize = size;
        }
    }
    
    void tryFixReceiptSize( uint64_t prepaidSize )
    {
        if ( prepaidSize >= totalReceiptSize() )
        {
            for( auto& cell: m_clientMap )
            {
                if ( cell.second.m_notAcceptedSize > cell.second.m_acceptedSize )
                {
                    cell.second.m_acceptedSize = cell.second.m_notAcceptedSize;
                    cell.second.m_notAcceptedSize = 0;
                }
            }
        }
    }
    
    uint64_t totalReceiptSize() const
    {
        uint64_t receiptSize = 0;
        for( auto& cell: m_clientMap )
        {
            if ( cell.second.m_notAcceptedSize > cell.second.m_acceptedSize )
            {
                receiptSize += cell.second.m_notAcceptedSize;
            }
            else
            {
                receiptSize += cell.second.m_acceptedSize;
            }
        }
        return receiptSize;
    }
    
    template <class Archive> void serialize( Archive & arch )
    {
        arch(m_clientMap);
    }
};

struct DownloadOpinionMapValue
{
    std::array<uint8_t,32>                              m_eventHash;
    std::array<uint8_t,32>                              m_downloadChannelId;
    std::map<std::array<uint8_t,32>, DownloadOpinion>   m_opinions;
    bool                                                m_modifyApproveTransactionSent = false;
    bool                                                m_approveTransactionReceived = false;
    Timer                                               m_timer = {};
    Timer                                               m_opinionShareTimer = {};
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

using ClientReceipts = std::map<ReplicatorKey,RcptMessage>;

struct DownloadChannelInfo
{
    struct ClientSizes
    {
        uint64_t m_sentSize = 0;

        template <class Archive> void serialize( Archive & arch )
        {
            arch( m_sentSize );
        }
    };

    bool     m_isSynchronizing = false;

    uint64_t m_prepaidDownloadSize;
    uint64_t m_totalReceiptsSize = 0;

    // realy sent sizes to clients
    std::map<ClientKey,ClientSizes> m_sentClientMap;
    
    // requested (by client) sizes
    using ReplicatorUploadRequestMap = std::map<ReplicatorKey,ReplicatorUploadRequestInfo>;
    ReplicatorUploadRequestMap      m_replicatorUploadRequestMap;

    std::array<uint8_t,32>  m_driveKey;
    ReplicatorList          m_dnReplicatorShard;
    
    DownloadOpinionMap      m_downloadOpinionMap;
    
    // receipts from all replicators
    std::map<ClientKey,ClientReceipts> m_clientReceiptMap = {};

    // it is used when drive is closing
    bool m_isClosed = false;

    // for saving/loading channel map before/after shutdown
    template <class Archive> void serialize( Archive & arch )
    {
        arch( m_prepaidDownloadSize );
        arch( m_driveKey );
        arch(m_totalReceiptsSize );
        arch( m_replicatorUploadRequestMap );
        arch( m_sentClientMap );
        arch( m_downloadOpinionMap );
        arch( m_clientReceiptMap );
    }
};

// key is a channel hash
using ChannelMap         = std::map<ChannelId, DownloadChannelInfo>;

//
// Replicator
//
class Replicator: public ModificationsExecutor
{
public:

    virtual ~Replicator() = default;

    virtual void start() = 0;

    virtual const Key& replicatorKey() const = 0;

    // All of the below functions return error string (or empty string)

    // It is called as soon as all drives and channels are added
    virtual void asyncInitializationFinished() = 0;

    // It will be called in 3 cases:
    // - when received transaction about drive creation (in this case 'actualRootHash' should be empty)
    // - when storage-extension restarts and initiates the drive (that already was created)
    // - when replicator joined to existing drive
    //
    virtual void asyncAddDrive( Key driveKey, mobj<AddDriveRequest>&& driveRequest ) = 0;

    // It is called when the Replicator is removed from the Drive
    virtual void asyncRemoveDrive( Key driveKey ) = 0;

    // It notifies about changes in drive replicator list
	virtual void asyncSetReplicators( Key driveKey, mobj<std::vector<Key>>&& replicatorKeys ) = 0;

    // It notifies about changes in modification shards
    virtual void asyncSetShardDonator( Key driveKey, mobj<std::vector<Key>>&& replicatorKeys ) = 0;
    virtual void asyncSetShardRecipient( Key driveKey, mobj<std::vector<Key>>&& replicatorKeys ) = 0;

    // It notifies about changes in download channel shard
    virtual void asyncSetChanelShard( mobj<Hash256>&& channelId, mobj<ReplicatorList>&& replicatorKeys ) = 0;

    // it starts drive closing
    virtual void asyncCloseDrive( Key driveKey, Hash256 transactionHash ) = 0;

#ifdef __FOR_DEBUGGING__
    //virtual std::shared_ptr<sirius::drive::FlatDrive> getDrive( const Key& driveKey ) = 0;
#endif
    
    // it begins modify operation, that will be performed on session thread
    virtual void        asyncModify( Key driveKey, mobj<ModificationRequest>&&  modifyRequest ) = 0;

    virtual void        asyncCancelModify( Key driveKey, Hash256  transactionHash ) = 0;
    
    virtual void        asyncStartDriveVerification( Key driveKey, mobj<VerificationRequest>&& ) = 0;
    virtual void        asyncCancelDriveVerification( Key driveKey ) = 0;

    virtual void        asyncStartStream( Key driveKey, mobj<StreamRequest>&& ) = 0;
    virtual void        asyncIncreaseStream( Key driveKey, mobj<StreamIncreaseRequest>&& ) = 0;
    virtual void        asyncFinishStreamTxPublished( Key driveKey, mobj<StreamFinishRequest>&& ) = 0;

    // It is called when Replicator is added to the Download Channel Shard
    virtual void        asyncAddDownloadChannelInfo( Key driveKey, mobj<DownloadRequest>&&  downloadRequest, bool mustBeSynchronized = false ) = 0;

	// It is called when the prepaid size of the download channel is increased
	virtual void		asyncIncreaseDownloadChannelSize( ChannelId channelId, uint64_t size ) = 0;

    // It is called when Replicator leaves the Download Channel Shard
    virtual void        asyncRemoveDownloadChannelInfo( ChannelId channelId ) = 0;

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
    
    // It will be called after 'MODIFY approval transaction' has been published
    virtual void        asyncApprovalTransactionHasBeenPublished( mobj<PublishedModificationApprovalTransactionInfo>&& transaction ) = 0;

    // It will be called if transaction sent by the Replicator has failed because of invalid Replicators list
    virtual void        asyncApprovalTransactionHasFailedInvalidOpinions( Key driveKey, Hash256 transactionHash ) = 0;

    // It will be called after 'single MODIFY approval transaction' has been published
    virtual void        asyncSingleApprovalTransactionHasBeenPublished( mobj<PublishedModificationSingleApprovalTransactionInfo>&& transaction ) = 0;

    // It will be called after 'VERIFY approval transaction' has been published
    virtual void        asyncVerifyApprovalTransactionHasBeenPublished( PublishedVerificationApprovalTransactionInfo info ) = 0;

    // It will be called if transaction sent by the Replicator has failed because of invalid Replicators list
    virtual void        asyncVerifyApprovalTransactionHasFailedInvalidOpinions( Key driveKey, Hash256 verificationId ) = 0;

#ifndef SKIP_GRPC
    virtual void        setServiceAddress( const std::string& address ) = 0;
    virtual void        enableSupercontractServer() = 0;
    virtual void        enableMessengerServer() = 0;
#endif
    
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
    virtual void        setVerificationShareTimerDelay( int milliseconds ) = 0;
    virtual int         getVerificationShareTimerDelay() = 0;
    virtual void        setMinReplicatorsNumber( uint64_t number ) = 0;
    virtual uint64_t    getMinReplicatorsNumber() = 0;
    virtual void        setSessionSettings(const lt::settings_pack&, bool localNodes) = 0;

    virtual void        addReplicatorKeyToKademlia( const Key& key ) = 0;
    virtual void        addReplicatorKeysToKademlia( const std::vector<Key>& keys ) = 0;
    virtual void        removeReplicatorKeyFromKademlia( const Key& keys ) = 0;
    virtual void        dbgTestKademlia( KademliaDbgFunc dbgFunc ) = 0;
    virtual void        dbgTestKademlia2( ReplicatorList& outReplicatorList ) {}
    virtual OptionalEndpoint dbgGetEndpoint( const Key& key ) { return {}; }


    virtual void        stopReplicator() = 0;

    // For RPC support
    virtual bool        isConnectionLost() const { return false; };
    
    // Functions for debugging
    //
    virtual Hash256     dbgGetRootHash( const DriveKey& driveKey ) = 0;
    virtual void        dbgPrintDriveStatus( const Key& driveKey ) = 0;
    virtual void        dbgPrintTrafficDistribution( std::array<uint8_t,32>  transactionHash ) = 0;
    virtual std::string dbgReplicatorName() const = 0;
    //virtual std::shared_ptr<sirius::drive::FlatDrive> dbgGetDrive( const std::array<uint8_t,32>& driveKey ) = 0;
    virtual const Key&  dbgReplicatorKey() const = 0;
    virtual void        dbgSetLogMode( uint8_t mode ) = 0;
    virtual void        dbgAllowCreateNonExistingDrives() = 0;
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
                                               const std::string& dbgReplicatorName = ""
);

}

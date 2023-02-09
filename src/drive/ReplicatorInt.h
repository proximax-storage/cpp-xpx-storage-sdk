/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include "types.h"
#include "plugins.h"
#include "drive/FlatDrive.h"
#include "drive/Replicator.h"
#include "crypto/Signer.h"
#include "drive/Session.h"

namespace sirius::drive {

//
// Replicator
//
class ReplicatorInt: public Replicator, public DhtMessageHandler
{
public:

    virtual ~ReplicatorInt()
    {
    }

    virtual const crypto::KeyPair& keyPair() const = 0;

#ifdef __FOR_DEBUGGING__
    //virtual std::shared_ptr<sirius::drive::FlatDrive> getDrive( const Key& driveKey ) = 0;
#endif

    virtual bool        isStopped() = 0;

    virtual void        executeOnBackgroundThread( const std::function<void()>& task ) = 0;

    // It will be called when a new opinion should be verified
    virtual void        processDownloadOpinion( const DownloadApprovalTransactionInfo& anOpinion ) = 0;

    // It will be called when a new opinion should be verified
    virtual void        processOpinion( const ApprovalTransactionInfo& anOpinion ) = 0;

    // It continues drive closing (initiates DownloadApprovalTransaction and then removes drive)
    virtual void        closeDriveChannels( const mobj<Hash256>& blockHash, const Key& driveKey ) = 0;
    
    virtual void        finishDriveClosure( const Key& driveKey ) = 0;

#ifdef COMMON_MODIFY_MAP//-
    // It will be used while drive closing
    virtual void        removeModifyDriveInfo( const std::array<uint8_t,32>& modifyTransactionHash ) = 0;
#endif
    
    // TODO:
    // They will be called after 'cancel modify transaction' has been published
//    virtual void        onTransactionCanceled( ApprovalTransactionInfo&& transaction ) = 0;
//    virtual void        onTransactionCanceledByClient( ApprovalTransactionInfo&& transaction ) = 0;
    
    // Message exchange
    virtual void        sendMessage( const std::string&     query,
                                     const ReplicatorKey&   replicatorKey,
                                     const std::vector<uint8_t>& ) = 0;
    
    virtual void        sendSignedMessage( const std::string&   query,
                                           const ReplicatorKey& replicatorKey,
                                           const std::vector<uint8_t>& ) = 0;
    
    virtual void        sendMessage( const std::string&             query,
                                     const ReplicatorKey&           replicatorKey,
                                     const std::string&             message ) = 0;

    // will be called from Sesion
    virtual void        onMessageReceived( const std::string& query, const std::string&, const boost::asio::ip::udp::endpoint& source ) = 0;
    virtual bool        createSyncRcpts( const DriveKey&, const ChannelId&, std::ostringstream& outOs, Signature& outSignature ) = 0;
    virtual void        onSyncRcptReceived( const lt::string_view& response, const lt::string_view& sign ) = 0;
    
    // will be called from Sesion
    // when it receives message from another replicator
    // (must be implemented by DownloadLimiter)
    virtual void acceptReceiptFromAnotherReplicator( const RcptMessage& message ) = 0;

#ifdef COMMON_MODIFY_MAP//-
    virtual ModifyTrafficInfo getMyDownloadOpinion( const Hash256& transactionHash ) const = 0;
#endif
    
    virtual DownloadChannelInfo* getDownloadChannelInfo( const std::array<uint8_t,32>& driveKey, const std::array<uint8_t,32>& downloadChannelHash ) = 0;

    //virtual std::string loadTorrent( const Key& driveKey, const InfoHash& infoHash ) = 0;
};

}

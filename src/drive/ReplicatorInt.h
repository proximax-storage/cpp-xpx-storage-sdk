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
#include "Session.h"

namespace sirius::drive {

//
// Replicator
//
class ReplicatorInt: public Replicator
{
public:

    virtual ~ReplicatorInt()
    {
    }

    virtual const crypto::KeyPair& keyPair() const = 0;

#ifdef __FOR_DEBUGGING__
    //virtual std::shared_ptr<sirius::drive::FlatDrive> getDrive( const Key& driveKey ) = 0;
#endif

    // It will be called when a new opinion should be verified
    virtual void        processDownloadOpinion( const DownloadApprovalTransactionInfo& anOpinion ) = 0;

    // It will be called when a new opinion should be verified
    virtual void        processOpinion( const ApprovalTransactionInfo& anOpinion ) = 0;

    // It continues drive closing (initiates DownloadApprovalTransaction and then removes drive)
    virtual void        closeDriveChannels( const mobj<Hash256>& blockHash, const Key& driveKey ) = 0;
    
    virtual void        finishDriveClosure( const Key& driveKey ) = 0;

    // It will be used while drive closing
    virtual void        removeModifyDriveInfo( const std::array<uint8_t,32>& modifyTransactionHash ) = 0;

    
    // TODO:
    // They will be called after 'cancel modify transaction' has been published
//    virtual void        onTransactionCanceled( ApprovalTransactionInfo&& transaction ) = 0;
//    virtual void        onTransactionCanceledByClient( ApprovalTransactionInfo&& transaction ) = 0;
    
    // Message exchange
    virtual void        sendMessage( const std::string& query,
                                     const std::array<uint8_t,32>&  replicatorKey,
                                     const std::vector<uint8_t>& ) = 0;
    
    virtual void        sendMessage( const std::string&             query,
                                     const std::array<uint8_t,32>&  replicatorKey,
                                     const std::string&             message ) = 0;

    // will be called from Sesion
    virtual void        onMessageReceived( const std::string& query, const std::string&, const boost::asio::ip::udp::endpoint& source ) = 0;
    virtual bool        createSyncOpinion( const DriveKey& driveKey, const ChannelId& channelId, std::ostringstream& os ) = 0;
    virtual void        onSyncRcptReceived( const std::string& retString ) = 0;
    
    // will be called from Sesion
    // when it receives message from another replicator
    // (must be implemented by DownloadLimiter)
    virtual bool acceptReceiptFromAnotherReplicator( RcptMessage&& message ) = 0;

    virtual ModifyTrafficInfo getMyDownloadOpinion( const Hash256& transactionHash ) const = 0;

    virtual DownloadChannelInfo* getDownloadChannelInfo( const std::array<uint8_t,32>& driveKey, const std::array<uint8_t,32>& downloadChannelHash ) = 0;

    //virtual std::string loadTorrent( const Key& driveKey, const InfoHash& infoHash ) = 0;
};

}

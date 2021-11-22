/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include "drive/Replicator.h"
#include <sirius_drive/session_delegate.h>
#include "crypto/Signer.h"
#include "types.h"

#include <map>
#include <shared_mutex>

namespace sirius::drive {

//
// DownloadLimiter - it manages all user files at replicator side
//
class DownloadLimiter : public Replicator,
                        public lt::session_delegate,
                        public std::enable_shared_from_this<DownloadLimiter>
{
protected:    
    // Replicator's keys
    const crypto::KeyPair& m_keyPair;

    std::shared_mutex   m_mutex;

    ChannelMap          m_downloadChannelMap;
    std::shared_mutex   m_downloadChannelMutex;

    DownloadOpinionMap  m_downloadOpinionMap;
    std::shared_mutex   m_downloadOpinionMutex;


    ModifyDriveMap      m_modifyDriveMap;

    uint64_t            m_receiptLimit = 32*1024; //1024*1024;

    const char*         m_dbgOurPeerName = "unset";

public:
    DownloadLimiter( const crypto::KeyPair& keyPair, const char* dbgOurPeerName );
    
    void printReport( const std::array<uint8_t,32>&  transactionHash );

    bool acceptConnection( const std::array<uint8_t,32>&  transactionHash,
                           const std::array<uint8_t,32>&  peerPublicKey ) override;

    void onDisconnected( const std::array<uint8_t,32>&  transactionHash,
                         const std::array<uint8_t,32>&  peerPublicKey,
                         int                            siriusFlags ) override;

    void printTrafficDistribution( const std::array<uint8_t,32>&  transactionHash ) override;
    
    virtual ModifyDriveInfo getMyDownloadOpinion( const Hash256& transactionHash ) override;


    bool checkDownloadLimit( const std::array<uint8_t,64>& /*signature*/,
                             const std::array<uint8_t,32>& downloadChannelId,
                            uint64_t                       downloadedSizeByClient ) override;

    uint8_t getUploadedSize( const std::array<uint8_t,32>& downloadChannelId ) override;

    void addChannelInfo( const std::array<uint8_t,32>&  channelId,
                         uint64_t                       prepaidDownloadSize,
                         const Key&                     driveKey,
                         const ReplicatorList&          replicatorsList,

                         const std::vector<std::array<uint8_t,32>>&  clients );

    void addModifyDriveInfo( const Key&             modifyTransactionHash,
                             const Key&             driveKey,
                             uint64_t               dataSize,
                             const Key&             clientPublicKey,
                             const ReplicatorList&  replicatorsList );

    void removeModifyDriveInfo( const std::array<uint8_t,32>& modifyTransactionHash ) override;

    void onPieceRequest( const std::array<uint8_t,32>&  transactionHash,
                           const std::array<uint8_t,32>&  receiverPublicKey,
                           uint64_t                       pieceSize ) override;
    
    void onPieceRequestReceived( const std::array<uint8_t,32>&  transactionHash,
                                 const std::array<uint8_t,32>&  receiverPublicKey,
                                 uint64_t                       pieceSize ) override;

    void onPieceSent( const std::array<uint8_t,32>&  transactionHash,
                      const std::array<uint8_t,32>&  receiverPublicKey,
                      uint64_t                       pieceSize ) override;
    
    void onPieceReceived( const std::array<uint8_t,32>&  transactionHash,
                          const std::array<uint8_t,32>&  senderPublicKey,
                          uint64_t                       pieceSize ) override;

    // will be called when one replicator informs another about downloaded size by client
    virtual void acceptReceiptFromAnotherReplicator( const std::array<uint8_t,32>&  downloadChannelId,
                                                     const std::array<uint8_t,32>&  clientPublicKey,
                                                     const std::array<uint8_t,32>&  replicatorPublicKey,
                                                     uint64_t                       downloadedSize,
                                                     const std::array<uint8_t,64>&  signature ) override;
    
    void removeChannelInfo( const std::array<uint8_t,32>& channelId );

    bool isClient() const override;

    void signHandshake( const uint8_t* bytes, size_t size, std::array<uint8_t,64>& signature ) override;

    bool verifyHandshake( const uint8_t*                 bytes,
                          size_t                         size,
                          const std::array<uint8_t,32>&  publicKey,
                          const std::array<uint8_t,64>&  signature ) override;
    
    void signReceipt( const std::array<uint8_t,32>& downloadChannelId,
                      const std::array<uint8_t,32>& replicatorPublicKey,
                      uint64_t                      downloadedSize,
                      std::array<uint8_t,64>&       outSignature ) override;

    bool verifyReceipt(  const std::array<uint8_t,32>&  downloadChannelId,
                         const std::array<uint8_t,32>&  clientPublicKey,
                         const std::array<uint8_t,32>&  replicatorPublicKey,
                         uint64_t                       downloadedSize,
                         const std::array<uint8_t,64>&  signature ) override;

    const std::array<uint8_t,32>& publicKey() override;

    const Key& replicatorKey() const override;

    virtual const crypto::KeyPair& keyPair() const override;

    uint64_t receivedSize( const std::array<uint8_t,32>&  peerPublicKey ) override;

    uint64_t requestedSize( const std::array<uint8_t,32>&  peerPublicKey ) override;

    const char* dbgOurPeerName() override;

    uint64_t receiptLimit() const override;

    // may be, it will be used to increase or decrease limit
    void setReceiptLimit( uint64_t newLimitInBytes ) override;
};

}

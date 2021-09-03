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


namespace sirius { namespace drive {

//
// Replicator
//
class Replicator
{
public:

    virtual ~Replicator() = default;

    virtual void start() = 0;

    virtual std::string addDrive(const Key& driveKey, size_t driveSize) = 0;

    virtual std::string modify( const Key&              driveKey,
                                const Key&              clientPublicKey,
                                const InfoHash&         infoHash,
                                const Hash256&          transactionHash,
                                const ReplicatorList&   replicatorList,
                                uint64_t                maxDataSize,
                                const DriveModifyHandler&   handler ) = 0;

    virtual Hash256     getRootHash( const Key& driveKey ) = 0;

    // 'replicatorsList' is used to notify other replictors
    // (it does not contain its own endpoint)
    virtual void        addDownloadChannelInfo( const std::array<uint8_t,32>&   channelKey,
                                                size_t                          prepaidDownloadSize,
                                                const ReplicatorList&           replicatorsList,
                                                std::vector<const Key>&&        clients ) = 0;

    // 'replicatorsList' is used to verify other replictors receipts
    // (it does not contain its own endpoint)
//    virtual void        addModifyTransactionInfo( const std::array<uint8_t,32>& hash,
//                                                  const Key&                    clientPublicKey,
//                                                  size_t                        prepaidDownloadSize,
//                                                  const ReplicatorList&         replicatorsList,
//                                                  std::vector<const Key>&&      clients ) = 0;

    virtual uint64_t    receiptLimit() const = 0;

    virtual void        setReceiptLimit( uint64_t newLimitInBytes ) = 0;
    
    virtual void        printDriveStatus( const Key& driveKey ) = 0;
    
    virtual void        printTrafficDistribution( const std::array<uint8_t,32>&  transactionHash ) = 0;

};

PLUGIN_API std::shared_ptr<Replicator> createDefaultReplicator(
                                               crypto::KeyPair&&,
                                               std::string&&  address,
                                               std::string&&  port,
                                               std::string&&  storageDirectory,
                                               std::string&&  sandboxDirectory,
                                               bool           useTcpSocket, // use TCP socket (instead of uTP)
                                               const char*    dbgReplicatorName = ""
);

}}

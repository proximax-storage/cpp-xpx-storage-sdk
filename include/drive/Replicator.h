/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include "types.h"
#include "plugins.h"
//#include "drive/Receipt.h"
#include "drive/FlatDrive.h"
#include "crypto/Signer.h"

//#include <boost/utility/string_view.hpp>
//#include "drive/log.h"
//#include "DriveService.h"
//#include "RpcTypes.h"
//#include "rpc/server.h"
//#include "rpc/this_handler.h"

//#include <future>


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

    virtual std::string modify(const Key& driveKey, const InfoHash& infoHash, const DriveModifyHandler& handler ) = 0;

    virtual Hash256     getRootHash( const Key& driveKey ) = 0;

    virtual void        addDownloadChannelInfo( const std::array<uint8_t,32>& channelKey, size_t prepaidDownloadSize, std::vector<const Key>&& clients ) = 0;

    virtual size_t      receiptLimit() const = 0;

    virtual void        setReceiptLimit( size_t newLimitInBytes ) = 0;

    virtual void        printDriveStatus( const Key& driveKey ) = 0;

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

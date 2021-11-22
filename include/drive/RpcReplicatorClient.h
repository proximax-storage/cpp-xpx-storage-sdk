/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include <filesystem>
#include "types.h"
#include "plugins.h"
#include "RpcTypes.h"
#include "rpc/client.h"
#include "rpc/server.h"
#include "ClientSession.h"
#include "Utils.h"
#include "FsTree.h"
#include "../../rpclib/include/rpc/server.h"
#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>

namespace sirius::drive {

class PLUGIN_API RpcReplicatorClient
{
public:
    using DownloadDataCallabck = std::function<void(download_status::code code,
                                                    const InfoHash& infoHash,
                                                    const std::filesystem::path filePath,
                                                    size_t downloaded,
                                                    size_t fileSize,
                                                    const std::string& errorText)>;

    using DownloadFsTreeCallback = std::function<void(const FsTree& fsTree,
                                                      download_status::code code)>;

public:
    RpcReplicatorClient();

    RpcReplicatorClient( const std::string& clientPrivateKey,
                         const std::string& remoteRpcAddress,
                         const int remoteRpcPort,
                         const std::string& incomingAddress,
                         const int incomingPort,
                         const int incomingRpcPort,
                         const std::filesystem::path& workFolder,
                         const std::string& dbgName);

    void addDrive(const Key& driveKey, const uint64_t driveSize);

    // TODO: Pass correct transaction hash
    void removeDrive(const Key& driveKey);

    types::RpcDriveInfo getDrive(const Key& driveKey);

    void openDownloadChannel(
            const std::array<uint8_t,32>&   channelKey,
            const size_t                    prepaidDownloadSize,
            const std::array<uint8_t,32>&   drivePubKey,
            const std::vector<Key>&        	clients);

    void closeDownloadChannel(const std::array<uint8_t,32>& channelKey);

    void modifyDrive( const Key& driveKey,
                      const ActionList& actionList,
                      const std::array<uint8_t,32>& transactionHash,
                      const uint64_t maxDataSize,
                      std::function<void()> endDriveModificationCallback);

    void downloadFsTree(const Key& drivePubKey,
                        const std::array<uint8_t,32>& channelKey,
                        DownloadFsTreeCallback callback,
                        const uint64_t downloadLimit = 0);

    void downloadData(const Folder& folder, DownloadDataCallabck callback);

    std::filesystem::path createClientFiles( size_t bigFileSize );

    void driveModificationIsCompleted(const types::RpcEndDriveModificationInfo& rpcEndDriveModificationInfo);

    void async();
    void sync();

    const Key &getPubKey() const;

private:
    std::thread m_rpcServerThread;
    std::map<std::array<uint8_t,32>, std::function<void()>> m_endDriveModificationHashes;
    std::shared_ptr<ClientSession> m_clientSession;
    std::shared_ptr<rpc::client> m_rpcClient;
    std::shared_ptr<rpc::server> m_rpcServer;
    Key m_clientPubKey;
    std::filesystem::path m_rootFolder;
    std::string m_address;
    int m_rpcPort;
};
}

/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include <filesystem>
#include <shared_mutex>
#include "types.h"
#include "plugins.h"
#include "RpcTypes.h"
#include "rpc/client.h"
#include "rpc/server.h"
#include "drive/ClientSession.h"
#include "drive/Utils.h"
#include "drive/FsTree.h"
#include "../../cpp-xpx-rpclib/include/rpc/server.h"
#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>

namespace sirius::emulator {

class PLUGIN_API RpcReplicatorClient
{
public:
    using DownloadDataCallabck = std::function<void(drive::download_status::code code,
                                                    const drive::InfoHash& infoHash,
                                                    const std::filesystem::path filePath,
                                                    size_t downloaded,
                                                    size_t fileSize,
                                                    const std::string& errorText)>;

    using DownloadFsTreeCallback = std::function<void(const drive::FsTree& fsTree,
                                                      drive::download_status::code code)>;

    using AddDriveCallback = std::function<void(const std::array<uint8_t,32>& drivePubKey)>;

public:
    RpcReplicatorClient( const crypto::KeyPair& keyPair,
                         const std::string& remoteRpcAddress,
                         const int remoteRpcPort,
                         const std::string& incomingAddress,
                         const int incomingPort,
                         const int incomingRpcPort,
                         const endpoint_list& bootstraps,
                         const std::filesystem::path& workFolder,
                         const std::string& dbgName);

    void addDrive(const Key& driveKey, const uint64_t driveSize, AddDriveCallback callback);

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
                      const drive::ActionList& actionList,
                      std::function<void()> endDriveModificationCallback);

    void downloadFsTree(const Key& drivePubKey,
                        const std::array<uint8_t,32>& channelKey,
                        const std::string& destinationFolder,
                        DownloadFsTreeCallback callback,
                        const uint64_t downloadLimit = 0);

    void downloadData(const drive::Folder& folder, const std::string& destinationFolder, const std::array<uint8_t,32>& channelKey, DownloadDataCallabck callback);
    void downloadData(const drive::InfoHash& hash, const std::string& tempFolder, const std::string& destinationFolder, const std::array<uint8_t,32>& channelKey, DownloadDataCallabck callback);

    std::filesystem::path createClientFiles( size_t bigFileSize );

    void driveModificationIsCompleted(const types::RpcEndDriveModificationInfo& rpcEndDriveModificationInfo);

    void driveAdded(const std::array<uint8_t,32>& drivePubKey);

    void async();
    void sync();

    const std::array<uint8_t,32>& getPubKey() const;

    static Hash256 getRandomHash();

private:
    const crypto::KeyPair& m_keyPair;
    std::thread m_rpcServerThread;
    std::map<std::array<uint8_t,32>, std::function<void()>> m_endDriveModificationHashes;
    std::map<std::array<uint8_t,32>, std::function<void(const std::array<uint8_t,32>& drivePubKey)>> m_addedDrives;
    std::shared_ptr<drive::ClientSession> m_clientSession;
    std::shared_ptr<rpc::client> m_rpcClient;
    std::shared_ptr<rpc::server> m_rpcServer;
    std::filesystem::path m_rootFolder;
    std::string m_address;
    std::shared_mutex m_addedDrivesMutex;
    std::shared_mutex m_endDriveModificationsMutex;
    int m_rpcPort;
};
}

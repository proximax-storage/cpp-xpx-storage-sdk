/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include <filesystem>
#include "types.h"
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

class RpcReplicatorClient
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
    RpcReplicatorClient( const std::string& clientPrivateKey,
                         const std::string& remoteRpcAddress,
                         const int remoteRpcPort,
                         const std::string& incomingAddress,
                         const int incomingPort,
                         const int incomingRpcPort,
                         const std::filesystem::path& workFolder,
                         const std::string& dbgName)
    {
        m_address = incomingAddress;
        m_rpcPort = incomingRpcPort;
        m_rootFolder = workFolder;
        m_rpcClient = std::make_shared<rpc::client>( remoteRpcAddress, remoteRpcPort );
        m_rpcClient->wait_all_responses();

        auto keyPair = sirius::crypto::KeyPair::FromPrivate(sirius::crypto::PrivateKey::FromString( clientPrivateKey ));
        m_clientPubKey = keyPair.publicKey();

        auto sessionHandler = []( const lt::alert* alert )
        {
            if ( alert->type() == lt::listen_failed_alert::alert_type )
            {
                std::cerr << alert->message() << std::endl << std::flush;
                exit(-1);
            }
        };

        m_clientSession = createClientSession(
                std::move(keyPair),
                incomingAddress + ":" + std::to_string(incomingPort),
                sessionHandler,
                true,
                dbgName.data() );

        bool isConnected = false;
        while (!isConnected) {
            switch (m_rpcClient->get_connection_state()) {
                case rpc::client::connection_state::initial:
                {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    continue;
                }
                case rpc::client::connection_state::connected:
                {
                    isConnected = true;
                }
                break;

                case rpc::client::connection_state::disconnected:
                case rpc::client::connection_state::reset:
                {
                    exit(500);
                }
            }
        }

        m_rpcServer = std::make_shared<rpc::server>( m_address, m_rpcPort );

        m_rpcServer->bind("driveModificationIsCompleted", [this](const types::RpcEndDriveModificationInfo& rpcEndDriveModificationInfo) {
            driveModificationIsCompleted(rpcEndDriveModificationInfo);
        });

        m_rpcServerThread = std::thread([rpcServer = m_rpcServer]{
            rpcServer->run();
        });
    }

    void addDrive(const Key& driveKey, const uint64_t driveSize)
    {
        std::cout << "Client. PrepareDriveTransaction: " << driveKey << std::endl;

        types::RpcPrepareDriveTransactionInfo rpcPrepareDriveTransactionInfo;
        rpcPrepareDriveTransactionInfo.m_clientPubKey = m_clientPubKey.array();
        rpcPrepareDriveTransactionInfo.m_driveKey = driveKey.array();
        rpcPrepareDriveTransactionInfo.m_driveSize = driveSize;
        rpcPrepareDriveTransactionInfo.m_signature = {};

        m_rpcClient->call( "PrepareDriveTransaction", rpcPrepareDriveTransactionInfo );
    }

    void removeDrive(const Key& driveKey)
    {
        std::cout << "Client. DriveClosureTransaction: " << driveKey << std::endl;
        m_rpcClient->call( "DriveClosureTransaction", driveKey.array());
    }

    types::RpcDriveInfo getDrive(const Key& driveKey) {
        std::cout << "Client. getDrive: " << driveKey << std::endl;
        return m_rpcClient->call( "drive", driveKey.array()).as<types::RpcDriveInfo>();
    }

    void openDownloadChannel(
            const std::array<uint8_t,32>&   channelKey,
            const size_t                    prepaidDownloadSize,
            const std::array<uint8_t,32>&   drivePubKey,
            const std::vector<Key>&        	clients) {
        std::cout << "Client. openDownloadChannel: " << utils::HexFormat(channelKey) << std::endl;

        types::RpcDriveInfo rpcDriveInfo = getDrive(drivePubKey);
        if (rpcDriveInfo.m_rpcReplicators.empty()) {
            std::cout << "Client. openDownloadChannel. Replicators list is empty: " << utils::HexFormat(drivePubKey) << std::endl;
            return;
        }

        m_clientSession->setDownloadChannel(rpcDriveInfo.getReplicators(), channelKey);

        types::RpcDownloadChannelInfo rpcDownloadChannelInfo;
        rpcDownloadChannelInfo.m_channelKey = channelKey;
        rpcDownloadChannelInfo.m_prepaidDownloadSize = prepaidDownloadSize;
        rpcDownloadChannelInfo.m_drivePubKey = drivePubKey;
        rpcDownloadChannelInfo.setClientsPublicKeys(clients);
        rpcDownloadChannelInfo.m_rpcReplicators = rpcDriveInfo.m_rpcReplicators;

        m_rpcClient->call( "openDownloadChannel", rpcDownloadChannelInfo );
    }

    void closeDownloadChannel(const std::array<uint8_t,32>& channelKey) {
        std::cout << "Client. closeDownloadChannel: " << utils::HexFormat(channelKey) << std::endl;
        m_rpcClient->call( "closeDownloadChannel", channelKey );
    }

    void modifyDrive( const Key& driveKey, const ActionList& actionList, const std::array<uint8_t,32>& transactionHash, const uint64_t maxDataSize, std::function<void()> endDriveModificationCallback) {
        std::cout << "Client. modifyDrive: " << driveKey << std::endl;

        types::RpcDriveInfo rpcDriveInfo = getDrive(driveKey);
        if (rpcDriveInfo.m_rpcReplicators.empty()) {
            std::cout << "Client. modifyDrive. Replicators list is empty: " << driveKey << std::endl;
            return;
        }

        // Create empty tmp folder for 'client modify data'
        //
        auto tmpFolder = std::filesystem::temp_directory_path() / "modify_drive_data";
        std::filesystem::remove_all( tmpFolder );
        std::filesystem::create_directories( tmpFolder );

        // start file uploading
        const InfoHash infoHash = m_clientSession->addActionListToSession( actionList, rpcDriveInfo.getReplicators(), transactionHash, tmpFolder );

        std::cout << "Client. modifyDrive. New InfoHash: " << infoHash << std::endl;

        types::RpcDataModification rpcDataModification;
        rpcDataModification.m_drivePubKey = driveKey.array();
        rpcDataModification.m_clientPubKey = m_clientPubKey.array();
        rpcDataModification.m_infoHash = infoHash.array();
        rpcDataModification.m_transactionHash = transactionHash;
        rpcDataModification.m_maxDataSize = maxDataSize;
        rpcDataModification.m_rpcReplicators = rpcDriveInfo.m_rpcReplicators;

        types::RpcClientInfo rpcClientInfo;
        rpcClientInfo.m_address = m_address;
        rpcClientInfo.m_rpcPort = m_rpcPort;
        rpcClientInfo.m_clientPubKey = rpcDataModification.m_clientPubKey;

        if (m_endDriveModificationHashes.contains(rpcDataModification.m_transactionHash)) {
            std::cout << "Client. modifyDrive. Hash already exists: " << utils::HexFormat(rpcDataModification.m_transactionHash) << std::endl;
        } else {
            m_endDriveModificationHashes.insert(std::pair<std::array<uint8_t,32>, std::function<void()>>(rpcDataModification.m_transactionHash, endDriveModificationCallback));
        }

        m_rpcClient->call( "DataModificationTransaction", rpcDataModification, rpcClientInfo );
    }

    void downloadFsTree(const Key& drivePubKey,
                        const std::array<uint8_t,32>& channelKey,
                        DownloadFsTreeCallback callback,
                        const uint64_t downloadLimit = 0) {

        types::RpcDriveInfo rpcDriveInfo = getDrive(drivePubKey);
        if (rpcDriveInfo.m_rpcReplicators.empty()) {
            std::cout << "Client. downloadFsTree. Replicators list is empty: " << drivePubKey << std::endl;
            return;
        }

        std::cout << "Client. downloadFsTree. channelKey: " << utils::HexFormat(channelKey) << std::endl;
        std::cout << "Client. downloadFsTree. InfoHash: " << utils::HexFormat(rpcDriveInfo.m_rootHash) << std::endl;

        auto handler = [this, callback](download_status::code code,
                                  const InfoHash& infoHash,
                                  const std::filesystem::path filePath,
                                  size_t downloaded,
                                  size_t fileSize,
                                  const std::string& errorText) {
            FsTree fsTree;
            if ( code == download_status::complete )
            {
                std::cout << "Client. downloadHandler. Client received FsTree: " << toString(infoHash) << std::endl;

                fsTree.deserialize( m_rootFolder / "fsTree-folder" / "FsTree.bin" );
                fsTree.dbgPrint();

                callback(fsTree, code);
            }
            else if ( code == download_status::failed )
            {
                std::cout << "Client. downloadHandler. Error receiving FsTree: " << code << std::endl;
                callback(fsTree, code);
            }
        };

        DownloadContext downloadContext( DownloadContext::fs_tree, handler, rpcDriveInfo.m_rootHash, channelKey, 0 );
        m_clientSession->download( std::move(downloadContext), m_rootFolder / "fsTree-folder");
    }

    void downloadData(const Folder& folder, DownloadDataCallabck callback) {
        std::cout << "Client. downloadData. Folder: " << folder.name() << std::endl;

        for( const auto& child: folder.childs() )
        {
            if ( isFolder(child) )
            {
                downloadData( getFolder(child), callback );
            }
            else
            {
                const File& file = getFile(child);
                std::string folderName = "root";
                if ( folder.name() != "/" )
                    folderName = folder.name();

                std::cout << "Client. downloadData. Client started download file " << internalFileName( file.hash() ) << std::endl;
                std::cout << "Client. downloadData. to " << m_rootFolder / "downloaded_files" / folderName  / file.name() << std::endl;

                auto handler = [callback](download_status::code code,
                                              const InfoHash& infoHash,
                                              const std::filesystem::path filePath,
                                              size_t downloaded,
                                              size_t fileSize,
                                              const std::string& errorText) {
                    callback(code, infoHash, filePath, downloaded, fileSize, errorText);
                };

                DownloadContext downloadContext(
                        DownloadContext::file_from_drive, handler, file.hash(), {}, 0, m_rootFolder / "downloaded_files" / folderName / file.name() );

                m_clientSession->download( std::move(downloadContext), m_rootFolder / "downloaded_files" );
            }
        }
    }

    std::filesystem::path createClientFiles( size_t bigFileSize ) {

        std::cout << "Client. createClientFiles." << std::endl;

        // Create empty tmp folder for testing
        //
        auto dataFolder = m_rootFolder / "client_files";
        std::filesystem::remove_all( dataFolder.parent_path() );
        std::filesystem::create_directories( dataFolder );

        {
            std::ofstream file( dataFolder / "a.txt" );
            file.write( "a_txt", 5 );
            file.close();
        }
        {
            std::filesystem::path b_bin = dataFolder / "b.bin";
            std::filesystem::create_directories( b_bin.parent_path() );
            std::vector<uint8_t> data(bigFileSize);
            std::generate( data.begin(), data.end(), std::rand );
            std::ofstream file( b_bin );
            file.write( (char*) data.data(), data.size() );
            file.close();
        }
        {
            std::ofstream file( dataFolder / "c.txt" );
            file.write( "c_txt", 5 );
            file.close();
        }
        {
            std::ofstream file( dataFolder / "d.txt" );
            file.write( "d_txt", 5 );
            file.close();
        }

        // Return path to file
        return dataFolder.parent_path();
    }

    void driveModificationIsCompleted(const types::RpcEndDriveModificationInfo& rpcEndDriveModificationInfo) {
        std::cout << "Client. driveModificationIsCompleted." << std::endl;

        if (m_endDriveModificationHashes.contains(rpcEndDriveModificationInfo.m_modifyTransactionHash)) {
            m_endDriveModificationHashes[rpcEndDriveModificationInfo.m_modifyTransactionHash]();
        } else {
            std::cout << "Client. driveModificationIsCompleted. Hash not found: " << utils::HexFormat(rpcEndDriveModificationInfo.m_modifyTransactionHash) << std::endl;
        }
    }

    void wait() {
        m_rpcServerThread.join();
    }

    const Key &getPubKey() const {
        return m_clientPubKey;
    }

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

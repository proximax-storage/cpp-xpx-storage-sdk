/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "drive/RpcReplicatorClient.h"
namespace sirius::drive {
    RpcReplicatorClient::RpcReplicatorClient( const crypto::KeyPair& keyPair,
                                              const std::string& remoteRpcAddress,
                                              const int remoteRpcPort,
                                              const std::string& incomingAddress,
                                              const int incomingPort,
                                              const int incomingRpcPort,
                                              const std::filesystem::path& workFolder,
                                              const std::string& dbgName)
                                              : m_keyPair(keyPair)
    {
        m_address = incomingAddress;
        m_rpcPort = incomingRpcPort;
        m_rootFolder = workFolder;
        m_rpcClient = std::make_shared<rpc::client>( remoteRpcAddress, remoteRpcPort );
        m_rpcClient->wait_all_responses();

        auto sessionHandler = []( const lt::alert* alert )
        {
            if ( alert->type() == lt::listen_failed_alert::alert_type )
            {
                std::cerr << alert->message() << std::endl << std::flush;
                exit(-1);
            }
        };

        m_clientSession = createClientSession(
                m_keyPair,
                incomingAddress + ":" + std::to_string(incomingPort),
                sessionHandler,
                false,
                dbgName.data() );

        std::cout << "Client. Public key: " << utils::HexFormat(m_keyPair.publicKey().array()) << std::endl;

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

        m_rpcServer->bind("driveAdded", [this](const std::array<uint8_t,32>& drivePubKey) {
            driveAdded(drivePubKey);
        });

        m_rpcServerThread = std::thread([rpcServer = m_rpcServer]{
            rpcServer->run();
        });
    }

    void RpcReplicatorClient::addDrive(const Key& driveKey, const uint64_t driveSize, AddDriveCallback callback)
    {
        std::cout << "Client. PrepareDriveTransaction: " << driveKey << std::endl;

        types::RpcPrepareDriveTransactionInfo rpcPrepareDriveTransactionInfo;
        rpcPrepareDriveTransactionInfo.m_clientPubKey = getPubKey();
        rpcPrepareDriveTransactionInfo.m_driveKey = driveKey.array();
        rpcPrepareDriveTransactionInfo.m_driveSize = driveSize;
        rpcPrepareDriveTransactionInfo.m_signature = {};

        types::RpcClientInfo rpcClientInfo;
        rpcClientInfo.m_address = m_address;
        rpcClientInfo.m_rpcPort = m_rpcPort;
        rpcClientInfo.m_clientPubKey = rpcPrepareDriveTransactionInfo.m_clientPubKey;

        rpcPrepareDriveTransactionInfo.m_rpcClientInfo = rpcClientInfo;

        if (m_addedDrives.contains(driveKey.array())) {
            std::cout << "Client. addDrive. Hash already exists: " << driveKey << std::endl;
        } else {
            m_addedDrives.insert(std::pair<std::array<uint8_t,32>, std::function<void(const std::array<uint8_t,32>& drivePubKey)>>(driveKey.array(), callback));
        }

        m_rpcClient->call( "PrepareDriveTransaction", rpcPrepareDriveTransactionInfo );
    }

    // TODO: Pass correct transaction hash
    void RpcReplicatorClient::removeDrive(const Key& driveKey)
    {
        std::cout << "Client. DriveClosureTransaction: " << driveKey << std::endl;

        // TODO: Pass correct transaction hash
        m_rpcClient->call( "DriveClosureTransaction", driveKey.array());
    }

    types::RpcDriveInfo RpcReplicatorClient::getDrive(const Key& driveKey) {
        std::cout << "Client. getDrive: " << driveKey << std::endl;
        return m_rpcClient->call( "drive", driveKey.array()).as<types::RpcDriveInfo>();
    }

    void RpcReplicatorClient::openDownloadChannel(
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

    void RpcReplicatorClient::closeDownloadChannel(const std::array<uint8_t,32>& channelKey) {
        std::cout << "Client. closeDownloadChannel: " << utils::HexFormat(channelKey) << std::endl;
        m_rpcClient->call( "closeDownloadChannel", channelKey );
    }

    void RpcReplicatorClient::modifyDrive( const Key& driveKey,
                                           const ActionList& actionList,
                                           const uint64_t maxDataSize,
                                           std::function<void()> endDriveModificationCallback) {
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
        const InfoHash infoHash = m_clientSession->addActionListToSession( actionList, rpcDriveInfo.getReplicators(), tmpFolder );

        std::cout << "Client. modifyDrive. New InfoHash: " << infoHash << std::endl;

        types::RpcDataModification rpcDataModification;
        rpcDataModification.m_drivePubKey = driveKey.array();
        rpcDataModification.m_clientPubKey = getPubKey();
        rpcDataModification.m_infoHash = infoHash.array();
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

    void RpcReplicatorClient::downloadFsTree(const Key& drivePubKey,
                                             const std::array<uint8_t,32>& channelKey,
                                             DownloadFsTreeCallback callback,
                                             const uint64_t downloadLimit) {

        types::RpcDriveInfo rpcDriveInfo = getDrive(drivePubKey);
        if (rpcDriveInfo.m_rpcReplicators.empty()) {
            std::cout << "Client. downloadFsTree. Replicators list is empty: " << drivePubKey << std::endl;
            return;
        }

        std::cout << "Client. downloadFsTree. channelKey: " << utils::HexFormat(channelKey) << std::endl;
        std::cout << "Client. downloadFsTree. InfoHash: " << utils::HexFormat(rpcDriveInfo.m_rootHash) << std::endl;

        auto handler = [this, drivePubKey, callback](download_status::code code,
                                  const InfoHash& infoHash,
                                  const std::filesystem::path filePath,
                                  size_t downloaded,
                                  size_t fileSize,
                                  const std::string& errorText) {
            FsTree fsTree;
            if ( code == download_status::download_complete )
            {
                std::cout << "Client. downloadHandler. Client received FsTree: " << toString(infoHash) << std::endl;

                fsTree.deserialize( m_rootFolder / "drives" / std::string(drivePubKey.begin(), drivePubKey.end()) / "fsTree-folder" / "FsTree.bin" );
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
        m_clientSession->download( std::move(downloadContext), m_rootFolder / "drives" / std::string(drivePubKey.begin(), drivePubKey.end()) / "fsTree-folder");
    }

    void RpcReplicatorClient::downloadData(const Folder& folder, DownloadDataCallabck callback) {
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

                std::cout << "Client. downloadData. Client started download file " << hashToFileName( file.hash() ) << std::endl;
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

    std::filesystem::path RpcReplicatorClient::createClientFiles( size_t bigFileSize ) {

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

    void RpcReplicatorClient::driveModificationIsCompleted(const types::RpcEndDriveModificationInfo& rpcEndDriveModificationInfo) {
        std::cout << "Client. driveModificationIsCompleted: " << utils::HexFormat(rpcEndDriveModificationInfo.m_modifyTransactionHash) << std::endl;

        if (m_endDriveModificationHashes.contains(rpcEndDriveModificationInfo.m_modifyTransactionHash)) {
            m_endDriveModificationHashes[rpcEndDriveModificationInfo.m_modifyTransactionHash]();
        } else {
            std::cout << "Client. driveModificationIsCompleted. Hash not found: " << utils::HexFormat(rpcEndDriveModificationInfo.m_modifyTransactionHash) << std::endl;
        }
    }

    void RpcReplicatorClient::driveAdded(const std::array<uint8_t,32>& drivePubKey) {
        std::cout << "Client. driveAdded." << utils::HexFormat(drivePubKey) << std::endl;

        if (m_addedDrives.contains(drivePubKey)) {
            m_addedDrives[drivePubKey](drivePubKey);
        } else {
            std::cout << "Client. driveAdded. Hash not found: " << utils::HexFormat(drivePubKey) << std::endl;
        }
    }

    void RpcReplicatorClient::async() {
        m_rpcServerThread.detach();
    }

    void RpcReplicatorClient::sync() {
        m_rpcServerThread.join();
    }

    const std::array<uint8_t,32>& RpcReplicatorClient::getPubKey() const {
        return m_keyPair.publicKey().array();
    }
}

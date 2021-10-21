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
#include "ClientSession.h"
#include "Utils.h"
#include "FsTree.h"
#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>

namespace sirius::drive {

class RpcReplicatorClient
{
public:
    RpcReplicatorClient( const std::string& clientPrivateKey,
                         const std::string& remoteRpcAddress,
                         const int remoteRpcPort,
                         const std::string& incomingAddress,
                         const int incomingPort,
                         const std::filesystem::path& workFolder,
                         const std::string& dbgName)
    {
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
            const std::vector<Key>&        	clients,
            const ReplicatorList&           replicatorList) {
        std::cout << "Client. openDownloadChannel: " << utils::HexFormat(channelKey) << std::endl;
        m_clientSession->setDownloadChannel(replicatorList, channelKey);

        types::RpcDownloadChannelInfo rpcDownloadChannelInfo;
        rpcDownloadChannelInfo.m_channelKey = channelKey;
        rpcDownloadChannelInfo.m_prepaidDownloadSize = prepaidDownloadSize;
        rpcDownloadChannelInfo.setClientsPublicKeys(clients);

        m_rpcClient->call( "openDownloadChannel", rpcDownloadChannelInfo );
    }

    void closeDownloadChannel(const std::array<uint8_t,32>& channelKey) {
        std::cout << "Client. closeDownloadChannel: " << utils::HexFormat(channelKey) << std::endl;
        m_rpcClient->call( "closeDownloadChannel", channelKey );
    }

    void modifyDrive( const Key& driveKey, const ActionList& actionList, const std::array<uint8_t,32>& transactionHash, const uint64_t maxDataSize ) {
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

        m_rpcClient->call( "DataModificationTransaction", rpcDataModification );
    }

    void downloadFsTree(const InfoHash& rootHash, const std::array<uint8_t,32>& channelKey, const uint64_t downloadLimit = 0) {
        std::cout << "Client. downloadFsTree. channelKey: " << utils::HexFormat(channelKey) << std::endl;
        std::cout << "Client. downloadFsTree. InfoHash: " << rootHash << std::endl;

        auto downloadHandler = [this](download_status::code code,
                                  const InfoHash& infoHash,
                                  const std::filesystem::path filePath,
                                  size_t downloaded,
                                  size_t fileSize,
                                  const std::string& errorText) {
            clientDownloadHandler(code, infoHash, filePath, downloaded, fileSize, errorText);
        };

        DownloadContext downloadContext( DownloadContext::fs_tree, downloadHandler, rootHash, channelKey, 0 );
        m_clientSession->download( std::move(downloadContext), m_rootFolder / "fsTree-folder");
    }

    void downloadData(const Folder& folder) {
        std::cout << "Client. downloadData. Folder: " << folder.name() << std::endl;

        for( const auto& child: folder.childs() )
        {
            if ( isFolder(child) )
            {
                downloadData( getFolder(child) );
            }
            else
            {
                const File& file = getFile(child);
                std::string folderName = "root";
                if ( folder.name() != "/" )
                    folderName = folder.name();

                std::cout << "Client. downloadData. Client started download file " << internalFileName( file.hash() ) << std::endl;
                std::cout << "Client. downloadData. to " << m_rootFolder / "downloaded_files" / folderName  / file.name() << std::endl;

                auto downloadHandler = [this](download_status::code code,
                                              const InfoHash& infoHash,
                                              const std::filesystem::path filePath,
                                              size_t downloaded,
                                              size_t fileSize,
                                              const std::string& errorText) {
                    clientDownloadHandler(code, infoHash, filePath, downloaded, fileSize, errorText);
                };

                DownloadContext downloadContext(
                        DownloadContext::file_from_drive, downloadHandler, file.hash(), {}, 0, m_rootFolder / "downloaded_files" / folderName / file.name() );

                m_clientSession->download( std::move(downloadContext), m_rootFolder / "downloaded_files" );
            }
        }
    }

    void clientDownloadHandler( download_status::code code,
                                const InfoHash& infoHash,
                                const std::filesystem::path /*filePath*/,
                                size_t /*downloaded*/,
                                size_t /*fileSize*/,
                                const std::string& /*errorText*/ )
    {
        if ( code == download_status::complete )
        {
            std::cout << "Client. clientDownloadHandler. Client received FsTree: " << toString(infoHash) << std::endl;

            FsTree fsTree;
            fsTree.deserialize( m_rootFolder / "fsTree-folder" / "FsTree.bin" );
            fsTree.dbgPrint();
        }
        else if ( code == download_status::failed )
        {
            std::cout << "Client. clientDownloadHandler. Error receiving FsTree: " << code << std::endl;
            exit(-1);
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

private:
    std::shared_ptr<ClientSession> m_clientSession;
    std::shared_ptr<rpc::client> m_rpcClient;
    Key m_clientPubKey;
    std::filesystem::path m_rootFolder;
};
}

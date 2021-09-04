/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "types.h"
#include "DriveService.h"
#include "RpcTypes.h"
#include "rpc/client.h"

namespace sirius::drive {
//
// RpcReplicatorClient
//
class RpcReplicatorClient
{
    std::shared_ptr<rpc::client>                    m_rpcClient;

public:

    RpcReplicatorClient( const std::string& address, int port )
    {
        m_rpcClient = std::make_shared<rpc::client>( address, port );
        m_rpcClient->wait_all_responses();

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

    bool addDrive(const Key& driveKey, size_t driveSize)
    {
        LOG( "RpcReplicatorClient: adding drive " << driveKey );

        auto reply = m_rpcClient->call( "addDrive", driveKey.array(), driveSize ).as<std::string>();

        return reply.empty();
//            CATAPULT_THROW_INVALID_ARGUMENT_1(reply, driveKey);
    }

    bool removeDrive(const Key& driveKey)
    {
        LOG( "RpcReplicatorClient: removing drive " << driveKey );

        auto error = m_rpcClient->call( "removeDrive", driveKey.array() ).as<std::string>();
        return error.empty();
    }

    bool openDownloadChannel(
            const std::array<uint8_t,32>&   channelKey,
            size_t                          prepaidDownloadSize,
            const ReplicatorList&           replicatorsList,
            std::vector<Key>&&        		clients) {
        types::DownloadChannelInfo channelInfo;
        channelInfo.m_channelKey = channelKey;
        channelInfo.m_prepaidDownloadSize = prepaidDownloadSize;
        channelInfo.setReplicators(replicatorsList);
        channelInfo.setClientsPublicKeys(clients);

        auto error = m_rpcClient->call( "openDownloadChannel", channelInfo ).as<std::string>();
        return error.empty();
    }

    bool closeDownloadChannel(const std::array<uint8_t,32>& channelKey) {
        auto error = m_rpcClient->call( "closeDownloadChannel", channelKey ).as<std::string>();
        return error.empty();
    }

    InfoHash getRootHash(const Key& driveKey)
    {
        LOG( "RpcReplicatorClient: getRootHash for " << driveKey );

        auto result = m_rpcClient->call( "getRootHash", driveKey.array() )
                        .as<types::ResultWithInfoHash>();

        if ( !result.m_error.empty() ) {
            CATAPULT_THROW_INVALID_ARGUMENT_1(result.m_error, driveKey)
        }

        return result.m_rootHash;
    }

    void modify( const Key& driveKey, const InfoHash& infoHash ) {
        LOG( "RpcReplicatorClient: drive modification:\n drive: " << driveKey << "\n info hash: " << infoHash );

        auto result = m_rpcClient->call( "modify", driveKey.array(), infoHash.array() ).as<types::ResultWithModifyStatus>();

        if ( !result.m_error.empty() ) {
            CATAPULT_THROW_INVALID_ARGUMENT_1(result.m_error, driveKey);
        }
    }

    void loadTorrent( const Key& driveKey, const InfoHash& infoHash ) {
        LOG( "RpcReplicatorClient: loadTorrent:\n drive: " << driveKey << "\n info hash: " << infoHash );

        auto result = m_rpcClient->call( "loadTorrent", driveKey.array(), infoHash.array() ).as<types::ResultWithModifyStatus>();

        if ( !result.m_error.empty() ) {
            CATAPULT_THROW_INVALID_ARGUMENT_1(result.m_error, driveKey);
        }
    }
};
}

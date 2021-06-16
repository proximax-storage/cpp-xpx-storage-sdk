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

namespace sirius { namespace drive {
//
// RpcReplicatorClient
//
class RpcReplicatorClient
{
    std::shared_ptr<rpc::client>                    m_rpcClient;

public:

    RpcReplicatorClient( std::string host, int port )
    {
        m_rpcClient = std::make_shared<rpc::client>( host, port );
    }

    bool addDrive(const Key& driveKey, size_t driveSize)
    {
        LOG( "RpcReplicatorClient: adding drive " << driveKey );

        auto reply =
                m_rpcClient->call( "addDrive", driveKey.array(), driveSize ).as<std::string>();

        return reply.empty();
//            CATAPULT_THROW_INVALID_ARGUMENT_1(reply, driveKey);
    }

    bool removeDrive(const Key& driveKey)
    {
        LOG( "RpcReplicatorClient: removing drive " << driveKey );

        std::string error = m_rpcClient->call( "removeDrive", driveKey.array() ).as<std::string>();

        return error.empty();
//        if ( !error.empty() ) {
//            CATAPULT_THROW_INVALID_ARGUMENT_1(error, driveKey);
//        }
    }

    InfoHash getRootHash(const Key& driveKey)
    {
        LOG( "RpcReplicatorClient: getRootHash for " << driveKey );

        auto result = m_rpcClient->call( "getRootHash", driveKey.array() )
                        .as<ResultWithInfoHash>();

        if ( !result.m_error.empty() ) {
            CATAPULT_THROW_INVALID_ARGUMENT_1(result.m_error, driveKey);
        }

        return result.m_rootHash;
    }

    void modify( const Key& driveKey, const InfoHash& infoHash ) {
        LOG( "RpcReplicatorClient: drive modification:\n drive: " << driveKey << "\n info hash: " << infoHash );

        auto result = m_rpcClient->call( "modify", driveKey.array(), infoHash.array() ).as<ResultWithModifyStatus>();

        if ( !result.m_error.empty() ) {
            CATAPULT_THROW_INVALID_ARGUMENT_1(result.m_error, driveKey);
        }
    }

    void loadTorrent( const Key& driveKey, const InfoHash& infoHash ) {
        LOG( "RpcReplicatorClient: loadTorrent:\n drive: " << driveKey << "\n info hash: " << infoHash );

        auto result = m_rpcClient->call( "loadTorrent", driveKey.array(), infoHash.array() ).as<ResultWithModifyStatus>();

        if ( !result.m_error.empty() ) {
            CATAPULT_THROW_INVALID_ARGUMENT_1(result.m_error, driveKey);
        }
    }

};

}}

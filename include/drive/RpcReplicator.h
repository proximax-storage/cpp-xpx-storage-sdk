/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "types.h"
#include "drive/log.h"
#include "drive/Replicator.h"
#include "RpcTypes.h"
#include "rpc/server.h"
#include "rpc/this_handler.h"
#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>

#include <future>


namespace sirius::drive {
//
// RpcReplicator
//
class RpcReplicator
{
public:

    RpcReplicator(
            const std::string&  privateKey,
            const std::string&  name,
            std::string&&       address,
            std::string&&       port,
            std::string&&       replicatorRootFolder,
            std::string&&       sandboxRootFolder,
            const int           rpcPort ) :

            m_replicatorRootFolder( replicatorRootFolder ),
            m_sandboxRootFolder( sandboxRootFolder ) {

        _LOG("RPC Replicator name: " + name)
        _LOG("RPC Replicator address: " + address)
        _LOG("RPC Replicator port: " + port)
        _LOG("RPC Replicator RPC port: " + std::to_string(rpcPort))
        _LOG("RPC Replicator root folder: " + replicatorRootFolder)
        _LOG("RPC Replicator sandbox folder: " + sandboxRootFolder)

        auto keyPair = sirius::crypto::KeyPair::FromPrivate(sirius::crypto::PrivateKey::FromString( privateKey ));

        m_rpcServer = std::make_shared<rpc::server>( address, rpcPort );

        m_replicator = createDefaultReplicator(
                std::move(keyPair),
                std::move(address),
                std::move(port),
                std::move(replicatorRootFolder),
                std::move(sandboxRootFolder),
                true, // tcp
                name.c_str()
        );

        m_replicator->start();

        //
        // addDrive
        //
        m_rpcServer->bind( "addDrive", [replicator = m_replicator]( const std::array<uint8_t, 32>& driveKey,
                                                                        size_t driveSize ) {
            auto reply = replicator->addDrive( reinterpret_cast<const sirius::Key&>(driveKey), driveSize );
            return reply;
        } );

        //
        // removeDrive
        //
        m_rpcServer->bind( "removeDrive", [replicator = m_replicator]( const std::array<uint8_t, 32>& driveKey ) {
            auto reply = replicator->removeDrive( reinterpret_cast<const sirius::Key&>(driveKey));
            return reply;
        } );

        //
        // openDownloadChannel
        //
        m_rpcServer->bind( "openDownloadChannel", [replicator = m_replicator](const types::DownloadChannelInfo& channelInfo) {
            replicator->addDownloadChannelInfo(
                    channelInfo.m_channelKey,
                    channelInfo.m_prepaidDownloadSize,
                    channelInfo.getReplicators(),
                    channelInfo.getClientsPublicKeys());

            return "";
        } );

        //
        // closeDownloadChannel
        //
        m_rpcServer->bind( "closeDownloadChannel", [replicator = m_replicator]( std::array<uint8_t, 32> channelKey ) {
            replicator->removeDownloadChannelInfo(channelKey);
            return "";
        } );

        //
        // getRootHash
        //
        m_rpcServer->bind( "getRootHash", [replicator = m_replicator]( const std::array<uint8_t, 32>& driveKey ) {
            try {
                InfoHash hash = replicator->getRootHash( reinterpret_cast<const sirius::Key&>(driveKey));

                types::ResultWithInfoHash result{hash.array(), ""};

                return result;
            }
            catch ( std::runtime_error& err ) {
                types::ResultWithInfoHash result;
                result.m_error = err.what();
                return result;
            }
        } );

        //
        // modify
        //
        m_rpcServer->bind( "modify", [replicator = m_replicator, replicators = m_replicatorList]( const std::array<uint8_t, 32>& driveKey,
                                                                  const std::array<uint8_t, 32>& hash ) {
            auto drivePubKey = reinterpret_cast<const sirius::Key&>(driveKey);
            auto infoHash = reinterpret_cast<const InfoHash&>(hash);
            std::promise<types::ResultWithModifyStatus> promise;
            auto future = promise.get_future();

            Hash256 transactionHash;
            uint64_t maxDataSize = 100;

            // TODO: fix to pass correct client key
            replicator->modify( drivePubKey, drivePubKey, infoHash, transactionHash, replicators, maxDataSize, [&promise, driveKey = drivePubKey](
                    sirius::drive::modify_status::code code,
                    const sirius::drive::InfoHash& rootHash,
                    const std::string& error ) {
                switch ( code ) {
                    case sirius::drive::modify_status::update_completed: {
                        LOG( " drive update completed:\n drive key: " << driveKey << "\n root hash: "
                                                                      << sirius::drive::toString(
                                                                              rootHash ));
                        types::ResultWithModifyStatus result{sirius::drive::modify_status::update_completed,
                                                      ""};
                        promise.set_value( result );
                        break;
                    }
                    case sirius::drive::modify_status::sandbox_root_hash: {
                        LOG( " drive modified in sandbox:\n drive key: " << driveKey
                                                                         << "\n root hash: "
                                                                         << sirius::drive::toString(
                                                                                 rootHash ));
                        break;
                    }
                    case sirius::drive::modify_status::failed: {
                        LOG_ERR( " drive modification failed:\n drive key: " << driveKey
                                                                             << "\n error: "
                                                                             << error );
                        types::ResultWithModifyStatus result{sirius::drive::modify_status::failed, error};
                        promise.set_value( result );
                        break;
                    }
                    case sirius::drive::modify_status::broken: {
                        LOG_ERR( " drive modification aborted:\n drive key: " << driveKey );
                        types::ResultWithModifyStatus result{sirius::drive::modify_status::broken, ""};
                        promise.set_value( result );
                        return;
                    }
                }
            } );

            rpc::this_handler().respond( future.get());
        } );

        //
        // loadTorrent
        //
        m_rpcServer->bind( "loadTorrent", [replicator = m_replicator]( const std::array<uint8_t,32>& driveKey,
                                                                           const std::array<uint8_t,32>& hash )
        {
            auto drivePubKey = reinterpret_cast<const sirius::Key&>(driveKey);
            auto infoHash = reinterpret_cast<const InfoHash&>(hash);
            try {
                auto reply = replicator->loadTorrent( drivePubKey, infoHash );
                return reply;
            }
            catch ( std::runtime_error& err ) {
                return std::string(err.what());
            }
        });
    }

    void runRpcServer()
    {
        _LOG("RPC Replicator started")
        m_rpcServer->run();
    }

private:

//    void replicatorDownloadHandler ( modify_status::code code, InfoHash /*resultRootInfoHash*/, std::string error )
//    {
//        if ( code == modify_status::update_completed )
//        {
//            LOG( "@ update_completed: " );
//        }
//        else if ( code == modify_status::sandbox_root_hash )
//        {
//            LOG( "@ sandbox calculated" );
//        }
//        else
//        {
//            LOG( "ERROR: " << error );
//        }
//    }

        static void sessionErrorHandler( const lt::alert* alert)
        {
            if ( alert->type() == lt::listen_failed_alert::alert_type )
            {
                std::cerr << alert->message() << std::endl << std::flush;
                exit(-1);
            }
        }

    private:

        std::string m_replicatorRootFolder;
        std::string m_sandboxRootFolder;
        std::shared_ptr<Replicator> m_replicator;
        std::shared_ptr<rpc::server> m_rpcServer;
        ReplicatorList m_replicatorList;
};
}

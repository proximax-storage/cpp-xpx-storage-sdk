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

#define DRIVE_PUB_KEY                   std::array<uint8_t,32>{1,0,0,0,0,5,6,7,8,9, 0,1,2,3,4,5,6,7,8,9, 0,1,2,3,4,5,6,7,8,9, 0,1}


namespace sirius::drive {
//
// ReplicatorEventHandler
//
class MyReplicatorEventHandler : public ReplicatorEventHandler
{
public:
    // It will be called before 'replicator' shuts down
    virtual void willBeTerminated( Replicator& replicator ) override
    {
        std::cout << "Replicator will be terminated: " << replicator.dbgReplicatorName() << std::endl;
    }

    // It will be called when rootHash is calculated in sandbox
    virtual void rootHashIsCalculated( Replicator&                    replicator,
                                       const sirius::Key&             driveKey,
                                       const sirius::drive::InfoHash& modifyTransactionHash,
                                       const sirius::drive::InfoHash& sandboxRootHash )  override
    {
        // send DataModificationApprovalTransaction
        std::cout << "@ send DataModificationApprovalTransaction: " << replicator.dbgReplicatorName() << std::endl;
    }

    // It will be called when transaction could not be completed
    virtual void modifyTransactionIsCanceled( Replicator& replicator,
                                              const sirius::Key&             driveKey,
                                              const sirius::drive::InfoHash& modifyTransactionHash,
                                              const std::string&             reason,
                                              int                            errorCode )  override
    {
        std::cout << "modifyTransactionIsCanceled: " << replicator.dbgReplicatorName() << std::endl;
    }

    // It will initiate the approving of modify transaction
    virtual void modifyApproveTransactionIsReady( Replicator& replicator, ApprovalTransactionInfo&& transactionInfo )  override
    {
        std::cout << "modifyApproveTransactionIsReady: " << replicator.dbgReplicatorName() << std::endl;
    }

    // It will initiate the approving of single modify transaction
    virtual void singleModifyApproveTransactionIsReady( Replicator& replicator, ApprovalTransactionInfo&& transactionInfo )  override
    {
        std::cout << "modifyTransactionIsCanceled: " << replicator.dbgReplicatorName() << std::endl;
    }

    // It will be called after the drive is synchronized with sandbox
    virtual void driveModificationIsCompleted( Replicator&                    replicator,
                                               const sirius::Key&             driveKey,
                                               const sirius::drive::InfoHash& modifyTransactionHash,
                                               const sirius::drive::InfoHash& rootHash ) override
    {
        std::cout << "@ driveModificationIsCompleted:" << replicator.dbgReplicatorName() << std::endl;
    }
};

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
            const int           rpcPort ) {

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
                m_replicatorEventHandler,
                name.c_str()
        );

        m_rpcServer->bind( "addDrive", [this]( const std::array<uint8_t, 32>& driveKey, const size_t driveSize ) {
            addDrive(driveKey, driveSize);
        } );

        m_rpcServer->bind( "removeDrive", [this]( const std::array<uint8_t, 32>& driveKey ) {
            removeDrive(driveKey);
        } );

        m_rpcServer->bind( "openDownloadChannel", [this](const types::RpcDownloadChannelInfo& channelInfo) {
            openDownloadChannel(channelInfo);
        } );

        m_rpcServer->bind( "closeDownloadChannel", [this](const std::array<uint8_t, 32>& channelKey) {
            closeDownloadChannel(channelKey);
        } );

//        m_rpcServer->bind( "getRootHash", [replicator = m_replicator]( const std::array<uint8_t, 32>& driveKey ) {
//            try {
//                InfoHash hash = replicator->getRootHash( reinterpret_cast<const sirius::Key&>(driveKey));
//
//                types::ResultWithInfoHash result{hash.array(), ""};
//
//                return result;
//            }
//            catch ( std::runtime_error& err ) {
//                types::ResultWithInfoHash result;
//                result.m_error = err.what();
//                return result;
//            }
//        } );

        m_rpcServer->bind( "modifyDrive", [this](const types::RpcDataModification& rpcDataModification) {
            modifyDrive(rpcDataModification);
        } );

//        //
//        // loadTorrent
//        //
//        m_rpcServer->bind( "loadTorrent", [replicator = m_replicator]( const std::array<uint8_t,32>& driveKey,
//                                                                           const std::array<uint8_t,32>& hash )
//        {
//            auto drivePubKey = reinterpret_cast<const sirius::Key&>(driveKey);
//            auto infoHash = reinterpret_cast<const InfoHash&>(hash);
//            try {
//                auto reply = replicator->loadTorrent( drivePubKey, infoHash );
//                return reply;
//            }
//            catch ( std::runtime_error& err ) {
//                return std::string(err.what());
//            }
//        });
    }

public:
    void run()
    {
        std::cout << "RPC Replicator is started" << std::endl;
        m_replicator->start();
        m_rpcServer->run();
    }

private:
    void addDrive(const std::array<uint8_t, 32>& driveKey, const size_t driveSize) {
        m_replicator->addDrive( reinterpret_cast<const sirius::Key&>(driveKey), driveSize );
    }

    void removeDrive(const std::array<uint8_t, 32>& driveKey) {
        m_replicator->removeDrive( reinterpret_cast<const sirius::Key&>(driveKey));
    }

    void modifyDrive(const types::RpcDataModification& rpcDataModification) {
        m_replicator->modify(DRIVE_PUB_KEY, ModifyRequest{
                rpcDataModification.m_infoHash,
                rpcDataModification.m_transactionHash,
                rpcDataModification.m_maxDataSize,
                rpcDataModification.getReplicators(),
                rpcDataModification.m_clientPubKey });
    }

    void openDownloadChannel(const types::RpcDownloadChannelInfo& channelInfo) {
        m_replicator->addDownloadChannelInfo(
                channelInfo.m_channelKey,
                channelInfo.m_prepaidDownloadSize,
                channelInfo.getReplicators(),
                channelInfo.getClientsPublicKeys());
    }

    void closeDownloadChannel(const std::array<uint8_t, 32>& channelKey) {
        m_replicator->removeDownloadChannelInfo(channelKey);
    }

private:
    std::shared_ptr<Replicator> m_replicator;
    std::shared_ptr<rpc::server> m_rpcServer;
    MyReplicatorEventHandler m_replicatorEventHandler;
};
}

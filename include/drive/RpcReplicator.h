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
#include <rpc/client.h>

//#define DRIVE_PUB_KEY                   std::array<uint8_t,32>{1,0,0,0,0,5,6,7,8,9, 0,1,2,3,4,5,6,7,8,9, 0,1,2,3,4,5,6,7,8,9, 0,1}


namespace sirius::drive {

class RpcReplicator : public ReplicatorEventHandler
{
public:
    RpcReplicator(
            const std::string&      privateKey,
            const std::string&      name,
            std::string&&           address,
            std::string&&           port,
            std::string&&           replicatorRootFolder,
            std::string&&           sandboxRootFolder,
            const unsigned short    rpcPort,
            const std::string&      emulatorRpcAddress,
            const unsigned short    emulatorRpcPort) {

        _LOG("RPC Replicator name: " + name)
        _LOG("RPC Replicator address: " + address)
        _LOG("RPC Replicator port: " + port)
        _LOG("RPC Replicator RPC port: " + std::to_string(rpcPort))
        _LOG("RPC Replicator root folder: " + replicatorRootFolder)
        _LOG("RPC Replicator sandbox folder: " + sandboxRootFolder)

        m_replicatorAddress = address;
        m_replicatorPort = std::stoi(port);
        m_rpcReplicatorPort = rpcPort;
        m_emulatorRpcAddress = emulatorRpcAddress;
        m_emulatorRpcPort = emulatorRpcPort;

        auto keyPair = sirius::crypto::KeyPair::FromPrivate(sirius::crypto::PrivateKey::FromString( privateKey ));

        m_rpcServer = std::make_shared<rpc::server>( address, rpcPort );

        m_replicator = createDefaultReplicator(
                std::move(keyPair),
                std::move(address),
                std::move(port),
                std::move(replicatorRootFolder),
                std::move(sandboxRootFolder),
                true, // tcp
                *this,
                name.c_str()
        );

        m_rpcServer->bind( "PrepareDriveTransaction", [this]( const types::RpcPrepareDriveTransactionInfo& rpcPrepareDriveTransactionInfo ) {
            addDrive(rpcPrepareDriveTransactionInfo);
        } );

        m_rpcServer->bind( "DriveClosureTransaction", [this]( const std::array<uint8_t, 32>& driveKey ) {
            removeDrive(driveKey);
        } );

        m_rpcServer->bind( "drive", [this]( const std::array<uint8_t, 32>& driveKey ) {
            return getDrive(driveKey);
        } );

        m_rpcServer->bind( "openDownloadChannel", [this](const types::RpcDownloadChannelInfo& channelInfo) {
            openDownloadChannel(channelInfo);
        } );

        m_rpcServer->bind( "closeDownloadChannel", [this](const std::array<uint8_t, 32>& channelKey) {
            closeDownloadChannel(channelKey);
        } );

        m_rpcServer->bind( "modifyDrive", [this](const types::RpcDataModification& rpcDataModification) {
            modifyDrive(rpcDataModification);
        } );

        m_rpcServer->bind( "acceptModifyApprovalTransaction", [this](const types::RpcModifyApprovalTransactionInfo& rpcModifyApprovalTransactionInfo) {
            acceptModifyApprovalTransaction(rpcModifyApprovalTransactionInfo);
        } );
    }

public:
    void run()
    {
        std::cout << "RPC Replicator is started. Replicator key: " << utils::HexFormat(m_replicator->keyPair().publicKey().array()) << std::endl;
        m_replicator->start();
        replicatorOnboardingTransaction();
        m_rpcServer->run();
    }

    // It will be called before 'replicator' shuts down
    virtual void willBeTerminated( Replicator& replicator ) override
    {
        std::cout << "willBeTerminated: Replicator key: " << utils::HexFormat(replicator.keyPair().publicKey().array()) << std::endl;
        std::cout << "willBeTerminated. Replicator will be terminated: " << replicator.dbgReplicatorName() << std::endl;
    }

    // It will be called when rootHash is calculated in sandbox
    virtual void rootHashIsCalculated( Replicator&                    replicator,
                                       const sirius::Key&             driveKey,
                                       const sirius::drive::InfoHash& modifyTransactionHash,
                                       const sirius::drive::InfoHash& sandboxRootHash )  override
    {
        // send DataModificationApprovalTransaction
        std::cout << "modifyTransactionIsCanceled: Replicator key: " << utils::HexFormat(replicator.keyPair().publicKey().array()) << std::endl;
        std::cout << "modifyTransactionIsCanceled: " << replicator.dbgReplicatorName() << std::endl;
    }

    // It will be called when transaction could not be completed
    virtual void modifyTransactionIsCanceled( Replicator& replicator,
                                              const sirius::Key&             driveKey,
                                              const sirius::drive::InfoHash& modifyTransactionHash,
                                              const std::string&             reason,
                                              int                            errorCode )  override
    {
        std::cout << "modifyTransactionIsCanceled: Replicator key: " << utils::HexFormat(replicator.keyPair().publicKey().array()) << std::endl;
        std::cout << "modifyTransactionIsCanceled: " << replicator.dbgReplicatorName() << std::endl;
    }

    // It will initiate the approving of modify transaction
    virtual void modifyApproveTransactionIsReady( Replicator& replicator, ApprovalTransactionInfo&& transactionInfo ) override
    {
        std::cout << "modifyApproveTransactionIsReady: Replicator key: " << utils::HexFormat(replicator.keyPair().publicKey().array()) << std::endl;
        std::cout << "modifyApproveTransactionIsReady: " << replicator.dbgReplicatorName() << std::endl;

        rpc::client emulator(m_emulatorRpcAddress, m_emulatorRpcPort);
        emulator.call("modifyApproveTransactionIsReady", types::RpcModifyApprovalTransactionInfo::getRpcModifyApprovalTransactionInfo(std::move(transactionInfo)));
    }

    // It will initiate the approving of single modify transaction
    virtual void singleModifyApproveTransactionIsReady( Replicator& replicator, ApprovalTransactionInfo&& transactionInfo )  override
    {
        std::cout << "singleModifyApproveTransactionIsReady: Replicator key: " << utils::HexFormat(replicator.keyPair().publicKey().array()) << std::endl;
        std::cout << "singleModifyApproveTransactionIsReady: " << replicator.dbgReplicatorName() << std::endl;
    }

    // It will be called after the drive is synchronized with sandbox
    virtual void driveModificationIsCompleted( Replicator&                    replicator,
                                               const sirius::Key&             driveKey,
                                               const sirius::drive::InfoHash& modifyTransactionHash,
                                               const sirius::drive::InfoHash& rootHash ) override
    {
        std::cout << "driveModificationIsCompleted. DriveKey: " << driveKey << std::endl;
        std::cout << "driveModificationIsCompleted: Replicator key: " << utils::HexFormat(replicator.keyPair().publicKey().array()) << std::endl;
        replicator.printDriveStatus(driveKey);
    }

private:
    void addDrive(const types::RpcPrepareDriveTransactionInfo& rpcPrepareDriveTransactionInfo) {
        std::cout << "addDrive: " << utils::HexFormat(rpcPrepareDriveTransactionInfo.m_driveKey) << std::endl;
        m_replicator->addDrive(
                rpcPrepareDriveTransactionInfo.m_driveKey,
                rpcPrepareDriveTransactionInfo.m_driveSize,
                rpcPrepareDriveTransactionInfo.getReplicators() );
    }

    void removeDrive(const std::array<uint8_t, 32>& driveKey) {
        std::cout << "removeDrive: " << utils::HexFormat(driveKey) << std::endl;
        m_replicator->removeDrive(driveKey);
    }

    types::RpcDriveInfo getDrive(const std::array<uint8_t, 32>& driveKey) {
        std::cout << "getDrive: " << utils::HexFormat(driveKey) << std::endl;

        std::shared_ptr<sirius::drive::FlatDrive> drive = m_replicator->getDrive(driveKey);
        if (!drive){
            std::cout << "getDrive. Drive not found: " << utils::HexFormat(driveKey) << std::endl;
            return {};
        }

        types::RpcDriveInfo rpcDriveInfo;
        rpcDriveInfo.m_driveKey = drive->drivePublicKey().array();
        rpcDriveInfo.m_rootHash = drive->rootHash().array();

        // TODO: fix it
        //rpcDriveInfo.m_clientsPublicKeys = drive->;
        rpcDriveInfo.setReplicators(drive->getReplicators());

        return rpcDriveInfo;
    }

    void modifyDrive(const types::RpcDataModification& rpcDataModification) {
        std::cout << "modifyDrive: " << utils::HexFormat(rpcDataModification.m_drivePubKey) << std::endl;
        m_replicator->modify(rpcDataModification.m_drivePubKey, ModifyRequest{
                rpcDataModification.m_infoHash,
                rpcDataModification.m_transactionHash,
                rpcDataModification.m_maxDataSize,
                rpcDataModification.getReplicators(),
                rpcDataModification.m_clientPubKey });
    }

    void openDownloadChannel(const types::RpcDownloadChannelInfo& channelInfo) {
        std::cout << "openDownloadChannel: " << utils::HexFormat(channelInfo.m_channelKey) << std::endl;
        m_replicator->addDownloadChannelInfo(
                channelInfo.m_channelKey,
                channelInfo.m_prepaidDownloadSize,
                channelInfo.getReplicators(),
                channelInfo.getClientsPublicKeys());
    }

    void closeDownloadChannel(const std::array<uint8_t, 32>& channelKey) {
        std::cout << "closeDownloadChannel: " << utils::HexFormat(channelKey) << std::endl;
        m_replicator->removeDownloadChannelInfo(channelKey);
    }

    void acceptModifyApprovalTransaction(const types::RpcModifyApprovalTransactionInfo& rpcModifyApprovalTransactionInfo) {
        std::cout << "acceptModifyApprovalTransaction. DriveKey: " << utils::HexFormat(rpcModifyApprovalTransactionInfo.m_drivePubKey) << std::endl;
        std::cout << "acceptModifyApprovalTransaction. RootHash: " << utils::HexFormat(rpcModifyApprovalTransactionInfo.m_rootHash) << std::endl;

        m_replicator->onApprovalTransactionHasBeenPublished(rpcModifyApprovalTransactionInfo.getApprovalTransactionInfo());
    }

    void replicatorOnboardingTransaction() {
        std::cout << "replicatorOnboardingTransaction: " << utils::HexFormat(m_replicator->keyPair().publicKey().array()) << std::endl;

        types::RpcReplicatorInfo rpcReplicatorInfo;
        rpcReplicatorInfo.m_replicatorPubKey = m_replicator->keyPair().publicKey().array();
        rpcReplicatorInfo.m_rpcReplicatorPort = m_rpcReplicatorPort;
        rpcReplicatorInfo.m_replicatorAddress = m_replicatorAddress;
        rpcReplicatorInfo.m_replicatorPort = m_replicatorPort;

        rpc::client emulator(m_emulatorRpcAddress, m_emulatorRpcPort);
        emulator.call("ReplicatorOnboardingTransaction", rpcReplicatorInfo);
    }

private:
    std::shared_ptr<Replicator> m_replicator;
    std::shared_ptr<rpc::server> m_rpcServer;
    std::string m_replicatorAddress;
    std::string m_emulatorRpcAddress;
    unsigned short m_rpcReplicatorPort;
    unsigned short m_replicatorPort;
    unsigned short m_emulatorRpcPort;
};
}

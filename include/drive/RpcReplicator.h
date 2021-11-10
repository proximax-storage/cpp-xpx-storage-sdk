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

        m_rpcServer->bind( "acceptSingleModifyApprovalTransaction", [this](const types::RpcModifyApprovalTransactionInfo& rpcModifyApprovalTransactionInfo) {
            acceptSingleModifyApprovalTransaction(rpcModifyApprovalTransactionInfo);
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

    void setModifyApprovalTransactionTimerDelay(int milliseconds) {
        m_replicator->setModifyApprovalTransactionTimerDelay(milliseconds);
    }

    void setDownloadApprovalTransactionTimerDelay(int milliseconds) {
        m_replicator->setDownloadApprovalTransactionTimerDelay(milliseconds);
    }

    // It will be called before 'replicator' shuts down
    void willBeTerminated( Replicator& replicator ) override
    {
        std::cout << "Replicator. willBeTerminated: Replicator key: " << utils::HexFormat(replicator.keyPair().publicKey().array()) << std::endl;
        std::cout << "Replicator. willBeTerminated. Replicator will be terminated: " << replicator.dbgReplicatorName() << std::endl;
    }

    // It will be called when rootHash is calculated in sandbox
    void rootHashIsCalculated( Replicator&                    replicator,
                                       const sirius::Key&             driveKey,
                                       const sirius::drive::InfoHash& modifyTransactionHash,
                                       const sirius::drive::InfoHash& sandboxRootHash )  override
    {
        // send DataModificationApprovalTransaction
        std::cout << "Replicator. modifyTransactionRootHashIsCalculated: Replicator key: " << utils::HexFormat(replicator.keyPair().publicKey().array()) << std::endl;
        std::cout << "Replicator. modifyTransactionRootHashIsCalculated: " << replicator.dbgReplicatorName() << std::endl;
    }

    // It will be called when transaction could not be completed
    void modifyTransactionEndedWithError( Replicator& replicator,
                                              const sirius::Key&             driveKey,
                                              const sirius::drive::InfoHash& modifyTransactionHash,
                                              const std::string&             reason,
                                              int                            errorCode )  override
    {
        std::cout << "Replicator. modifyTransactionIsCanceled: Replicator key: " << utils::HexFormat(replicator.keyPair().publicKey().array()) << std::endl;
        std::cout << "Replicator. modifyTransactionIsCanceled: " << replicator.dbgReplicatorName() << std::endl;
    }

    // It will initiate the approving of modify transaction
    void modifyApprovalTransactionIsReady( Replicator& replicator, ApprovalTransactionInfo&& transactionInfo ) override
    {
        std::cout << "Replicator. modifyApproveTransactionIsReady: Replicator key: " << utils::HexFormat(replicator.keyPair().publicKey().array()) << std::endl;
        std::cout << "Replicator. modifyApproveTransactionIsReady: " << replicator.dbgReplicatorName() << std::endl;

        rpc::client emulator(m_emulatorRpcAddress, m_emulatorRpcPort);
        emulator.call("modifyApproveTransactionIsReady", types::RpcModifyApprovalTransactionInfo::getRpcModifyApprovalTransactionInfo(
                replicator.keyPair().publicKey().array(), std::move(transactionInfo)));
    }

    // It will initiate the approving of single modify transaction
    void singleModifyApprovalTransactionIsReady( Replicator& replicator, ApprovalTransactionInfo&& transactionInfo )  override
    {
        std::cout << "Replicator. singleModifyApproveTransactionIsReady: Replicator key: " << utils::HexFormat(replicator.keyPair().publicKey().array()) << std::endl;
        std::cout << "Replicator. singleModifyApproveTransactionIsReady: " << replicator.dbgReplicatorName() << std::endl;

        rpc::client emulator(m_emulatorRpcAddress, m_emulatorRpcPort);
        emulator.call("singleModifyApproveTransactionIsReady", types::RpcModifyApprovalTransactionInfo::getRpcModifyApprovalTransactionInfo(
                replicator.keyPair().publicKey().array(), std::move(transactionInfo)));
    }

    // It will be called after the drive is synchronized with sandbox
    // DataModificationApproval send to BC 2/3 of replicators
    void driveModificationIsCompleted( Replicator&                            replicator,
                                               const sirius::Key&             driveKey,
                                               const sirius::drive::InfoHash& modifyTransactionHash,
                                               const sirius::drive::InfoHash& rootHash ) override
    {
        std::cout << "Replicator. driveModificationIsCompleted. DriveKey: " << driveKey << std::endl;
        std::cout << "Replicator. driveModificationIsCompleted: Replicator key: " << utils::HexFormat(replicator.keyPair().publicKey().array()) << std::endl;
        replicator.printDriveStatus(driveKey);

        types::RpcEndDriveModificationInfo rpcEndDriveModificationInfo;
        rpcEndDriveModificationInfo.m_modifyTransactionHash = modifyTransactionHash.array();
        rpcEndDriveModificationInfo.m_replicatorInfo.m_replicatorPubKey = replicator.keyPair().publicKey().array();

        rpc::client emulator(m_emulatorRpcAddress, m_emulatorRpcPort);
        emulator.call("driveModificationIsCompleted", rpcEndDriveModificationInfo);
    }

    void downloadApprovalTransactionIsReady( Replicator& replicator, const DownloadApprovalTransactionInfo& ) override {
        std::cout << "Replicator. downloadApprovalTransactionIsReady. DriveKey: " << std::endl;
    }

private:
    void addDrive(const types::RpcPrepareDriveTransactionInfo& rpcPrepareDriveTransactionInfo) {
        std::cout << "Replicator. addDrive: " << utils::HexFormat(rpcPrepareDriveTransactionInfo.m_driveKey) << std::endl;
        m_replicator->addDrive(
                rpcPrepareDriveTransactionInfo.m_driveKey,
                rpcPrepareDriveTransactionInfo.m_driveSize,
                rpcPrepareDriveTransactionInfo.getReplicators() );
    }

    void removeDrive(const std::array<uint8_t, 32>& driveKey) {
        std::cout << "Replicator. removeDrive: " << utils::HexFormat(driveKey) << std::endl;

        // TODO: Pass correct transaction hash
        m_replicator->removeDrive(driveKey, driveKey);
    }

    types::RpcDriveInfo getDrive(const std::array<uint8_t, 32>& driveKey) {
        std::cout << "Replicator. getDrive: " << utils::HexFormat(driveKey) << std::endl;

        std::shared_ptr<sirius::drive::FlatDrive> drive = m_replicator->getDrive(driveKey);
        if (!drive){
            std::cout << "Replicator. getDrive. Drive not found: " << utils::HexFormat(driveKey) << std::endl;
            return {};
        }

        types::RpcDriveInfo rpcDriveInfo;
        rpcDriveInfo.m_driveKey = drive->drivePublicKey().array();
        rpcDriveInfo.m_rootHash = drive->rootHash().array();

        // TODO: fix it
        //rpcDriveInfo.m_clientsPublicKeys = drive->;

        rpcDriveInfo.setReplicators(drive->getReplicators());

        // Add itself back to replicators list
        types::RpcReplicatorInfo ri;
        ri.m_replicatorPubKey = m_replicator->replicatorKey().array();
        ri.m_replicatorAddress = m_replicatorAddress;
        ri.m_replicatorPort = m_replicatorPort;
        ri.m_rpcReplicatorPort = m_rpcReplicatorPort;

        rpcDriveInfo.m_rpcReplicators.push_back(ri);

        return rpcDriveInfo;
    }

    void modifyDrive(const types::RpcDataModification& rpcDataModification) {
        std::cout << "Replicator. modifyDrive: " << m_replicator->dbgReplicatorName() << " : " << utils::HexFormat(rpcDataModification.m_drivePubKey) << std::endl;
        std::cout << "Replicator. modifyDrive: " << m_replicator->dbgReplicatorName() << " : " << utils::HexFormat(rpcDataModification.m_infoHash) << std::endl;

        m_replicator->modify(rpcDataModification.m_drivePubKey, ModifyRequest{
                rpcDataModification.m_infoHash,
                rpcDataModification.m_transactionHash,
                rpcDataModification.m_maxDataSize,
                rpcDataModification.getReplicators(),
                rpcDataModification.m_clientPubKey });
    }

    void openDownloadChannel(const types::RpcDownloadChannelInfo& channelInfo) {
        std::cout << "Replicator. openDownloadChannel: " << utils::HexFormat(channelInfo.m_channelKey) << std::endl;
        m_replicator->addDownloadChannelInfo(
                channelInfo.m_channelKey,
                channelInfo.m_prepaidDownloadSize,
                channelInfo.m_drivePubKey,
                channelInfo.getReplicators(),
                channelInfo.getClientsPublicKeys());
    }

    void closeDownloadChannel(const std::array<uint8_t, 32>& channelKey) {
        std::cout << "Replicator. closeDownloadChannel: " << utils::HexFormat(channelKey) << std::endl;
        m_replicator->removeDownloadChannelInfo(channelKey);
    }

    void acceptModifyApprovalTransaction(const types::RpcModifyApprovalTransactionInfo& rpcModifyApprovalTransactionInfo) {
        std::cout << "Replicator. acceptModifyApprovalTransaction. DriveKey: " << utils::HexFormat(rpcModifyApprovalTransactionInfo.m_drivePubKey) << std::endl;
        std::cout << "Replicator. acceptModifyApprovalTransaction. RootHash: " << utils::HexFormat(rpcModifyApprovalTransactionInfo.m_rootHash) << std::endl;

        // TODO: thread to avoid deadlock in synchronizeDriveWithSandbox
        std::thread([this, rpcModifyApprovalTransactionInfo]{
            m_replicator->onApprovalTransactionHasBeenPublished(rpcModifyApprovalTransactionInfo.getApprovalTransactionInfo());
        }).detach();
    }

    void acceptSingleModifyApprovalTransaction(const types::RpcModifyApprovalTransactionInfo& rpcModifyApprovalTransactionInfo) {
        std::cout << "Replicator. acceptSingleModifyApprovalTransaction. DriveKey: " << utils::HexFormat(rpcModifyApprovalTransactionInfo.m_drivePubKey) << std::endl;
        std::cout << "Replicator. acceptSingleModifyApprovalTransaction. RootHash: " << utils::HexFormat(rpcModifyApprovalTransactionInfo.m_rootHash) << std::endl;

        m_replicator->onSingleApprovalTransactionHasBeenPublished(rpcModifyApprovalTransactionInfo.getApprovalTransactionInfo());
    }

    void replicatorOnboardingTransaction() {
        std::cout << "Replicator. replicatorOnboardingTransaction: " << m_replicator->dbgReplicatorName() << " : " << utils::HexFormat(m_replicator->keyPair().publicKey().array()) << std::endl;

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

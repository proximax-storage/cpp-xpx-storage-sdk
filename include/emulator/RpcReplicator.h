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


namespace sirius::emulator {

using namespace sirius::drive;

class PLUGIN_API RpcReplicator : public ReplicatorEventHandler, DbgReplicatorEventHandler
{
public:
    RpcReplicator(
            const crypto::KeyPair&  keyPair,
            const std::string&      name,
            std::string&&           address,
            std::string&&           port,
            std::string&&           replicatorRootFolder,
            std::string&&           sandboxRootFolder,
            const unsigned short    rpcPort,
            const std::string&      emulatorRpcAddress,
            const unsigned short    emulatorRpcPort,
            const std::vector<ReplicatorInfo>& bootstraps)
            : m_keyPair(keyPair){

        std::cout << "RPC Replicator name: " << name << std::endl;
        std::cout << "RPC Replicator address: " << address << std::endl;
        std::cout << "RPC Replicator port: " << port << std::endl;
        std::cout << "RPC Replicator RPC port: " << std::to_string(rpcPort) << std::endl;
        std::cout << "RPC Replicator root folder: " << replicatorRootFolder << std::endl;
        std::cout << "RPC Replicator sandbox folder: " << sandboxRootFolder << std::endl;

        m_replicatorAddress = address;
        m_replicatorPort = std::stoi(port);
        m_rpcReplicatorPort = rpcPort;
        m_emulatorRpcAddress = emulatorRpcAddress;
        m_emulatorRpcPort = emulatorRpcPort;

        m_rpcServer = std::make_shared<rpc::server>( address, rpcPort );

        m_replicator = createDefaultReplicator(
                m_keyPair,
                std::move(address),
                std::move(port),
                std::move(replicatorRootFolder),
                std::move(sandboxRootFolder),
                bootstraps,
                false, // udp
                *this,
                this,
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
        std::cout << "Replicator. willBeTerminated: Replicator key: " << utils::HexFormat(replicator.dbgReplicatorKey().array()) << std::endl;
        std::cout << "Replicator. willBeTerminated. Replicator will be terminated: " << replicator.dbgReplicatorName() << std::endl;
    }

    // It will be called when rootHash is calculated in sandbox
    void rootHashIsCalculated( Replicator&                    replicator,
                                       const sirius::Key&             driveKey,
                                       const sirius::drive::InfoHash& modifyTransactionHash,
                                       const sirius::drive::InfoHash& sandboxRootHash )  override
    {
        // send DataModificationApprovalTransaction
        std::cout << "Replicator. modifyTransactionRootHashIsCalculated: Replicator key: " << utils::HexFormat(replicator.dbgReplicatorKey().array()) << std::endl;
        std::cout << "Replicator. modifyTransactionRootHashIsCalculated: " << replicator.dbgReplicatorName() << std::endl;
    }

    // It will be called when transaction could not be completed
    virtual void modifyTransactionEndedWithError( Replicator& replicator,
                                              const sirius::Key&             driveKey,
                                              const ModificationRequest&           modifyRequest,
                                              const std::string&             reason,
                                              int                            errorCode )  override
    {
        std::cout << "Replicator. modifyTransactionEndedWithError: Replicator key: " << utils::HexFormat(replicator.dbgReplicatorKey().array()) << std::endl;
        std::cout << "Replicator. modifyTransactionEndedWithError: " << replicator.dbgReplicatorName() << std::endl;
    }

    // It will initiate the approving of modify transaction
    void modifyApprovalTransactionIsReady( Replicator& replicator, const ApprovalTransactionInfo& transactionInfo ) override
    {
        std::cout << "Replicator. modifyApproveTransactionIsReady: Replicator key: " << utils::HexFormat(replicator.dbgReplicatorKey().array()) << std::endl;
        std::cout << "Replicator. modifyApproveTransactionIsReady: " << replicator.dbgReplicatorName() << std::endl;

        rpc::client emulator(m_emulatorRpcAddress, m_emulatorRpcPort);
        emulator.call("modifyApproveTransactionIsReady", types::RpcModifyApprovalTransactionInfo::getRpcModifyApprovalTransactionInfo(
                replicator.dbgReplicatorKey().array(), transactionInfo));
    }

    // It will initiate the approving of single modify transaction
    void singleModifyApprovalTransactionIsReady( Replicator& replicator, const ApprovalTransactionInfo& transactionInfo )  override
    {
        std::cout << "Replicator. singleModifyApproveTransactionIsReady: Replicator key: " << utils::HexFormat(replicator.dbgReplicatorKey().array()) << std::endl;
        std::cout << "Replicator. singleModifyApproveTransactionIsReady: " << replicator.dbgReplicatorName() << std::endl;

        rpc::client emulator(m_emulatorRpcAddress, m_emulatorRpcPort);
        emulator.call("singleModifyApproveTransactionIsReady", types::RpcModifyApprovalTransactionInfo::getRpcModifyApprovalTransactionInfo(
                replicator.dbgReplicatorKey().array(), transactionInfo));
    }

    // It will be called after the drive is synchronized with sandbox
    // DataModificationApproval send to BC 2/3 of replicators
    void driveModificationIsCompleted( Replicator&                            replicator,
                                               const sirius::Key&             driveKey,
                                               const sirius::drive::InfoHash& modifyTransactionHash,
                                               const sirius::drive::InfoHash& rootHash ) override
    {
        std::cout << "Replicator. driveModificationIsCompleted. DriveKey: " << driveKey << std::endl;
        std::cout << "Replicator. driveModificationIsCompleted: Replicator key: " << utils::HexFormat(replicator.dbgReplicatorKey().array()) << std::endl;
        replicator.dbgPrintDriveStatus(driveKey);

        types::RpcEndDriveModificationInfo rpcEndDriveModificationInfo;
        rpcEndDriveModificationInfo.m_modifyTransactionHash = modifyTransactionHash.array();
        rpcEndDriveModificationInfo.m_replicatorInfo.m_replicatorPubKey = replicator.dbgReplicatorKey().array();

        rpc::client emulator(m_emulatorRpcAddress, m_emulatorRpcPort);
        emulator.call("driveModificationIsCompleted", rpcEndDriveModificationInfo);
    }

    void downloadApprovalTransactionIsReady( Replicator& replicator, const DownloadApprovalTransactionInfo& ) override {
        std::cout << "Replicator. downloadApprovalTransactionIsReady. DriveKey: " << std::endl;
    }

    void opinionHasBeenReceived(  Replicator& replicator, const ApprovalTransactionInfo& ) override {
        std::cout << "Replicator. opinionHasBeenReceived. DriveKey: " << std::endl;
    }

    void downloadOpinionHasBeenReceived(  Replicator& replicator, const DownloadApprovalTransactionInfo& ) override {
        std::cout << "Replicator. downloadOpinionHasBeenReceived. DriveKey: " << std::endl;
    }

    void driveAdded(const sirius::Key& driveKey) override {
        rpc::client emulator(m_emulatorRpcAddress, m_emulatorRpcPort);
        emulator.call("driveAdded", driveKey.array());
    }

private:
    void addDrive(const types::RpcPrepareDriveTransactionInfo& rpcPrepareDriveTransactionInfo) {
        std::cout << "Replicator. addDrive: " << utils::HexFormat(rpcPrepareDriveTransactionInfo.m_driveKey) << std::endl;

        AddDriveRequest addDriveRequest;
        addDriveRequest.m_driveSize = rpcPrepareDriveTransactionInfo.m_driveSize;
        addDriveRequest.m_expectedCumulativeDownloadSize = 0;
        addDriveRequest.m_replicators = rpcPrepareDriveTransactionInfo.getReplicators();

        m_replicator->asyncAddDrive(rpcPrepareDriveTransactionInfo.m_driveKey, addDriveRequest );
    }

    void removeDrive(const std::array<uint8_t, 32>& driveKey) {
        std::cout << "Replicator. removeDrive: " << utils::HexFormat(driveKey) << std::endl;

        // TODO: Pass correct transaction hash
        m_replicator->asyncCloseDrive(driveKey, driveKey);
    }

    types::RpcDriveInfo getDrive(const std::array<uint8_t, 32>& driveKey) {
        std::cout << "Replicator. getDrive: " << utils::HexFormat(driveKey) << std::endl;

        std::shared_ptr<sirius::drive::FlatDrive> drive = m_replicator->dbgGetDrive(driveKey);
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
        rpcDriveInfo.m_rpcReplicators.push_back(m_replicator->dbgReplicatorKey().array());

        return rpcDriveInfo;
    }

    void modifyDrive(const types::RpcDataModification& rpcDataModification) {
        std::cout << "Replicator. modifyDrive: " << m_replicator->dbgReplicatorName() << " : "
                  << utils::HexFormat(rpcDataModification.m_drivePubKey) << " Info hash: "
                  << utils::HexFormat(rpcDataModification.m_infoHash) << std::endl;

        m_replicator->asyncModify(rpcDataModification.m_drivePubKey, ModificationRequest{
                rpcDataModification.m_infoHash,
                rpcDataModification.m_transactionHash,
                rpcDataModification.m_maxDataSize,
                rpcDataModification.getReplicators(),
                rpcDataModification.m_clientPubKey
        });
    }

    void openDownloadChannel(const types::RpcDownloadChannelInfo& channelInfo) {
        std::cout << "Replicator. openDownloadChannel: " << utils::HexFormat(channelInfo.m_channelKey) << std::endl;
        m_replicator->asyncAddDownloadChannelInfo(
                channelInfo.m_drivePubKey,
                {
                        channelInfo.m_channelKey,
                        channelInfo.m_prepaidDownloadSize,
                        channelInfo.getReplicators(),
                        channelInfo.getClientsPublicKeys()
                });
    }

    void closeDownloadChannel(const std::array<uint8_t, 32>& channelKey) {
        std::cout << "Replicator. closeDownloadChannel: " << utils::HexFormat(channelKey) << std::endl;
        // TODO Repair
        m_replicator->asyncRemoveDownloadChannelInfo({}, channelKey);
    }

    void acceptModifyApprovalTransaction(const types::RpcModifyApprovalTransactionInfo& rpcModifyApprovalTransactionInfo) {
        std::cout << "Replicator. acceptModifyApprovalTransaction. DriveKey: " << utils::HexFormat(rpcModifyApprovalTransactionInfo.m_drivePubKey) << std::endl;
        std::cout << "Replicator. acceptModifyApprovalTransaction. RootHash: " << utils::HexFormat(rpcModifyApprovalTransactionInfo.m_rootHash) << std::endl;

        // TODO: thread to avoid deadlock in synchronizeDriveWithSandbox
        std::thread([this, rpcModifyApprovalTransactionInfo]{
            m_replicator->asyncApprovalTransactionHasBeenPublished(rpcModifyApprovalTransactionInfo.getApprovalTransactionInfo());
        }).detach();
    }

    void acceptSingleModifyApprovalTransaction(const types::RpcModifyApprovalTransactionInfo& rpcModifyApprovalTransactionInfo) {
        std::cout << "Replicator. acceptSingleModifyApprovalTransaction. DriveKey: " << utils::HexFormat(rpcModifyApprovalTransactionInfo.m_drivePubKey) << std::endl;
        std::cout << "Replicator. acceptSingleModifyApprovalTransaction. RootHash: " << utils::HexFormat(rpcModifyApprovalTransactionInfo.m_rootHash) << std::endl;

        m_replicator->asyncSingleApprovalTransactionHasBeenPublished(rpcModifyApprovalTransactionInfo.getApprovalTransactionInfo());
    }

    void replicatorOnboardingTransaction() {
        std::cout << "Replicator. replicatorOnboardingTransaction: " << m_replicator->dbgReplicatorName() << " : "
                  << utils::HexFormat(m_replicator->dbgReplicatorKey().array()) << std::endl;

        types::RpcReplicatorInfo rpcReplicatorInfo;
        rpcReplicatorInfo.m_replicatorPubKey = m_replicator->dbgReplicatorKey().array();
        rpcReplicatorInfo.m_rpcReplicatorPort = m_rpcReplicatorPort;
        rpcReplicatorInfo.m_replicatorAddress = m_replicatorAddress;
        rpcReplicatorInfo.m_replicatorPort = m_replicatorPort;

        rpc::client emulator(m_emulatorRpcAddress, m_emulatorRpcPort);
        emulator.call("ReplicatorOnboardingTransaction", rpcReplicatorInfo);
    }

private:
    const crypto::KeyPair& m_keyPair;
    std::shared_ptr<Replicator> m_replicator;
    std::shared_ptr<rpc::server> m_rpcServer;
    std::string m_replicatorAddress;
    std::string m_emulatorRpcAddress;
    unsigned short m_rpcReplicatorPort;
    unsigned short m_replicatorPort;
    unsigned short m_emulatorRpcPort;
};
}

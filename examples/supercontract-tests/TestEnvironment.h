#pragma once

#include "crypto/Signer.h"
#include "drive/ClientSession.h"
#include "drive/FlatDrive.h"
#include "drive/FsTree.h"
#include "drive/Replicator.h"
#include "drive/Utils.h"
#include "types.h"
#include "utils.h"
#include "gtest/gtest.h"
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <future>
#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/kademlia/ed25519.hpp>
#include <numeric>
#include <sirius_drive/session_delegate.h>

namespace sirius::drive::test {

inline std::string intToString02(int n) {
    char str[10];
    snprintf(str, 7, "%02d", n);
    return str;
}

class DriveInfo {
public:
    AddDriveRequest m_driveRequest;
    std::deque<ModificationRequest> m_pendingModifications;
    std::optional<VerificationRequest> m_pendingVerification;
    std::optional<ApprovalTransactionInfo> m_lastApprovedModification;
    std::map<Key, std::vector<KeyAndBytes>> m_uploads;
};

class TestEnvironment : public ReplicatorEventHandler, DbgReplicatorEventHandler {
public:
    std::vector<std::shared_ptr<sirius::crypto::KeyPair>> m_keys;
    ReplicatorList m_addrList;
    std::vector<ReplicatorInfo> m_bootstraps;

    std::map<Hash256, std::condition_variable> modifyCompleteCondVars;
    std::map<Hash256, int> modifyCompleteCounters;
    std::mutex modifyCompleteMutex;

    std::map<Key, std::condition_variable> m_driveIsInitializedCondVars;
    std::map<Key, int> m_driveIsInitializedCounters;
    std::mutex m_driveIsInitializedMutex;

    std::map<Hash256, std::condition_variable> m_rootHashCalculatedCondVars;
    std::map<Hash256, int> m_rootHashCalculatedCounters;
    std::mutex m_rootHashCalculatedMutex;

    std::condition_variable driveClosedCondVar;
    std::atomic<unsigned int> driveClosedCounter{0};
    std::mutex driveClosedMutex;

    std::condition_variable m_downloadApprovedCondVar;

    std::optional<DownloadApprovalTransactionInfo> m_dnApprovalTransactionInfo;
    std::mutex m_transactionInfoMutex;

    std::condition_variable m_verificationCondVar;
    std::map<Hash256, VerifyApprovalTxInfo> m_verifyApprovalTransactionInfo;

    std::map<Hash256, InfoHash> m_rootHashes;

    std::map<Key, std::set<uint64_t>> m_modificationSizes;

    std::map<Key, DriveInfo> m_drives;

    std::vector<std::string> m_rootFolders;
    std::vector<std::string> m_sandboxFolders;
    std::vector<std::shared_ptr<Replicator>> m_replicators;

public:
    TestEnvironment(int numberOfReplicators,
                    std::string ipAddr0,
                    int port0,
                    std::string rootFolder0,
                    std::string sandboxRootFolder0,
                    bool useTcpSocket,
                    int modifyApprovalDelay,
                    int downloadApprovalDelay,
                    int startReplicator = -1,
                    bool startRpcServices = false) {
        if (startReplicator == -1) {
            startReplicator = numberOfReplicators;
        }
        for (int i = 1; i <= numberOfReplicators; i++) {
            std::string privateKey =
                intToString02(i) + "00000000010203040501020304050102030405010203040501020304050102";
            std::string ipAddr = ipAddr0 + intToString02(i);
            int port = port0 + i;
            std::string rootFolder = rootFolder0 + "_" + intToString02(i);
            std::string sandboxRootFolder = sandboxRootFolder0 + "_" + intToString02(i);
            std::string dbgReplicatorName = std::string("replicator_") + intToString02(i);

            auto keyPair = sirius::crypto::KeyPair::FromPrivate(
                sirius::crypto::PrivateKey::FromString(privateKey));
            m_keys.emplace_back(std::make_shared<sirius::crypto::KeyPair>(std::move(keyPair)));

            // First Replicator is a bootstrap node
            if (i == 1) {
                m_bootstraps = {{{boost::asio::ip::make_address(ipAddr), (unsigned short)port},
                                 m_keys.back()->publicKey()}};
            }

            // EXLOG( "creating: " << dbgReplicatorName << " with key: " <<  int(replicatorKeyPair.publicKey().array()[0]) );

            m_rootFolders.push_back(rootFolder);
            m_sandboxFolders.push_back(sandboxRootFolder);

            if (i <= startReplicator) {
                auto replicator = createDefaultReplicator(
                    *m_keys.back(),
                    std::move(ipAddr),
                    std::to_string(port),
                    std::move(rootFolder),
                    std::move(sandboxRootFolder),
                    m_bootstraps,
                    useTcpSocket,
                    *this,
                    this,
                    dbgReplicatorName.c_str());

                if (startRpcServices) {
                    replicator->setServiceAddress( "127.0.0.1:" + std::to_string( port ));
                    replicator->enableSupercontractServer();
                    replicator->enableMessengerServer();
                }

                replicator->setDownloadApprovalTransactionTimerDelay(modifyApprovalDelay);
                replicator->setModifyApprovalTransactionTimerDelay(downloadApprovalDelay);
                replicator->start();
                replicator->asyncInitializationFinished();
                m_replicators.emplace_back(replicator);
            } else {
                m_replicators.emplace_back(std::shared_ptr<Replicator>(nullptr));
            }

            m_addrList.emplace_back(m_keys.back()->publicKey());
        }
    }

    virtual void startReplicator(int i,
                                 std::string ipAddr0,
                                 int port0,
                                 std::string rootFolder0,
                                 std::string sandboxRootFolder0,
                                 bool useTcpSocket,
                                 int modifyApprovalDelay,
                                 int downloadApprovalDelay) {
        const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
        if (!m_replicators[i - 1]) {
            std::string privateKey =
                intToString02(i) + "00000000010203040501020304050102030405010203040501020304050102";
            std::string ipAddr = ipAddr0 + intToString02(i);
            int port = port0 + i;
            std::string rootFolder = rootFolder0 + "_" + intToString02(i);
            std::string sandboxRootFolder = sandboxRootFolder0 + "_" + intToString02(i);
            std::string dbgReplicatorName = std::string("replicator_") + intToString02(i);

            auto replicator = createDefaultReplicator(
                *m_keys[i - 1],
                std::move(ipAddr),
                std::to_string(port),
                std::move(rootFolder),
                std::move(sandboxRootFolder),
                m_bootstraps,
                useTcpSocket,
                *this,
                this,
                dbgReplicatorName.c_str());

            replicator->setDownloadApprovalTransactionTimerDelay(modifyApprovalDelay);
            replicator->setModifyApprovalTransactionTimerDelay(downloadApprovalDelay);
            replicator->start();
            m_replicators[i - 1] = replicator;
            for (const auto& [key, drive] : m_drives) {
                const auto& driveReplicators = drive.m_driveRequest.m_fullReplicatorList;
                if (std::find(driveReplicators.begin(), driveReplicators.end(), replicator->dbgReplicatorKey()) != driveReplicators.end()) {
                    replicator->asyncAddDrive(key, std::make_unique<AddDriveRequest>(drive.m_driveRequest));
                    if (drive.m_lastApprovedModification) {
                        replicator->asyncApprovalTransactionHasBeenPublished(std::make_unique<PublishedModificationApprovalTransactionInfo>(*drive.m_lastApprovedModification));
                    }
                    if (drive.m_pendingVerification) {
                        replicator->asyncStartDriveVerification(key, std::make_unique<VerificationRequest>(*drive.m_pendingVerification));
                    }
                    for (const auto& modification : drive.m_pendingModifications) {
                        replicator->asyncModify(key, std::make_unique<ModificationRequest>(modification));
                    }
                }
            }
            replicator->asyncInitializationFinished();
        }
    }

    void stopReplicator(int i) {
        if (m_replicators[i - 1]) {
            m_replicators[i - 1]->stopReplicator();
            m_replicators[i - 1].reset();
        }
    }

    virtual ~TestEnvironment() {
        for (auto& replicator: m_replicators) {
            replicator->stopReplicator();
        }
    }

    virtual void addDrive(const Key& driveKey,
                          const Key& client, uint64_t driveSize,
                          ReplicatorList replicators = {}) {
        if (replicators.empty()) {
            replicators = m_addrList;
        }
        m_drives[driveKey] = {{driveSize, 0, {}, replicators, client, replicators, replicators}, {}, {}, {}, {}};
        for (auto& key : replicators) {
            auto replicator = getReplicator(key);
            if (replicator) {
                replicator->asyncAddDrive(driveKey, std::make_unique<AddDriveRequest>(m_drives[driveKey].m_driveRequest));
            }
        }
    }

    virtual void addReplicatorToDrive(const Key& driveKey, const Key& replicatorKey) {
        const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);

        auto& drive = m_drives[driveKey];

        drive.m_driveRequest.m_fullReplicatorList.push_back(replicatorKey);
        drive.m_driveRequest.m_modifyDonatorShard.push_back(replicatorKey);
        drive.m_driveRequest.m_modifyRecipientShard.push_back(replicatorKey);

        for (const auto& r : drive.m_driveRequest.m_fullReplicatorList) {
            auto replicator = getReplicator(r);
            if (replicator && replicator->replicatorKey() != replicatorKey) {
                replicator->asyncSetReplicators(driveKey, std::make_unique<ReplicatorList>(drive.m_driveRequest.m_fullReplicatorList));

                auto donatorShard = drive.m_driveRequest.m_modifyDonatorShard;
                std::erase(donatorShard, replicator->replicatorKey());
                replicator->asyncSetShardDonator(driveKey, std::make_unique<ReplicatorList>(donatorShard));

                auto recipientShard = drive.m_driveRequest.m_modifyRecipientShard;
                std::erase(recipientShard, replicator->replicatorKey());
                replicator->asyncSetShardRecipient(driveKey, std::make_unique<ReplicatorList>(recipientShard));
            }
        }

        auto replicator = getReplicator(replicatorKey);
        if (replicator) {
            const auto& driveReplicators = drive.m_driveRequest.m_fullReplicatorList;
            if (std::find(driveReplicators.begin(), driveReplicators.end(), replicator->dbgReplicatorKey()) != driveReplicators.end()) {
                auto driveRequest = drive.m_driveRequest;
                std::erase(driveRequest.m_modifyDonatorShard, replicatorKey);
                std::erase(driveRequest.m_modifyRecipientShard, replicatorKey);

                replicator->asyncAddDrive(driveKey, std::make_unique<AddDriveRequest>(driveRequest));
                if (drive.m_lastApprovedModification) {
                    replicator->asyncApprovalTransactionHasBeenPublished(std::make_unique<PublishedModificationApprovalTransactionInfo>(*drive.m_lastApprovedModification));
                }
                for (const auto& modification : drive.m_pendingModifications) {
                    replicator->asyncModify(driveKey, std::make_unique<ModificationRequest>(modification));
                }
            }
        }
    }

    virtual void removeReplicatorFromDrive(const Key& driveKey, const Key& replicatorKey) {
        const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);

        auto& drive = m_drives[driveKey];

        std::erase(drive.m_driveRequest.m_fullReplicatorList, replicatorKey);
        std::erase(drive.m_driveRequest.m_modifyDonatorShard, replicatorKey);
        std::erase(drive.m_driveRequest.m_modifyRecipientShard, replicatorKey);

        for (const auto& r : drive.m_driveRequest.m_fullReplicatorList) {
            auto replicator = getReplicator(r);
            if (replicator) {
                replicator->asyncSetReplicators(driveKey, std::make_unique<ReplicatorList>(drive.m_driveRequest.m_fullReplicatorList));
                replicator->asyncSetShardDonator(driveKey, std::make_unique<ReplicatorList>(drive.m_driveRequest.m_modifyDonatorShard));
                replicator->asyncSetShardRecipient(driveKey, std::make_unique<ReplicatorList>(drive.m_driveRequest.m_modifyRecipientShard));
            }
        }

        auto replicator = getReplicator(replicatorKey);
        if (replicator) {
            replicator->asyncRemoveDrive(driveKey);
        }
    }

    virtual void modifyDrive(const Key& driveKey, const ModificationRequest& request) {
        const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
        m_drives[driveKey].m_pendingModifications.push_back(request);
        for (auto& key : m_drives[driveKey].m_driveRequest.m_fullReplicatorList) {
            auto replicator = getReplicator(key);
            if (replicator) {
                replicator->asyncModify(driveKey, std::make_unique<ModificationRequest>(request));
            }
        }
    }

    virtual void
    initiateManualModifications(const DriveKey& driveKey, const InitiateModificationsRequest& request) {
        const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
        for (auto& key : m_drives[driveKey].m_driveRequest.m_fullReplicatorList) {
            auto replicator = getReplicator(key);
            if (replicator) {
                replicator->initiateManualModifications(driveKey, request);
            }
        }
    }

    virtual void initiateManualSandboxModifications(const DriveKey& driveKey,
                                                    const InitiateSandboxModificationsRequest& request) {
        const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
        for (auto& key : m_drives[driveKey].m_driveRequest.m_fullReplicatorList) {
            auto replicator = getReplicator(key);
            if (replicator) {
                replicator->initiateManualSandboxModifications(driveKey, request);
            }
        }
    }

    virtual void openFile(const DriveKey& driveKey, const OpenFileRequest& request) {
        const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
        for (auto& key : m_drives[driveKey].m_driveRequest.m_fullReplicatorList) {
            auto replicator = getReplicator(key);
            if (replicator) {
                replicator->openFile(driveKey, request);
            }
        }
    }

    virtual void writeFile(const DriveKey& driveKey, const WriteFileRequest& request) {
        const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
        for (auto& key : m_drives[driveKey].m_driveRequest.m_fullReplicatorList) {
            auto replicator = getReplicator(key);
            if (replicator) {
                replicator->writeFile(driveKey, request);
            }
        }
    }

    virtual void readFile(const DriveKey& driveKey, const ReadFileRequest& request) {
        const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
        for (auto& key : m_drives[driveKey].m_driveRequest.m_fullReplicatorList) {
            auto replicator = getReplicator(key);
            if (replicator) {
                replicator->readFile(driveKey, request);
            }
        }
    }

    virtual void flush(const DriveKey& driveKey, const FlushRequest& request) {
        const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
        for (auto& key : m_drives[driveKey].m_driveRequest.m_fullReplicatorList) {
            auto replicator = getReplicator(key);
            if (replicator) {
                replicator->flush(driveKey, request);
            }
        }
    }

    virtual void closeFile(const DriveKey& driveKey, const CloseFileRequest& request) {
        const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
        for (auto& key : m_drives[driveKey].m_driveRequest.m_fullReplicatorList) {
            auto replicator = getReplicator(key);
            if (replicator) {
                replicator->closeFile(driveKey, request);
            }
        }
    }

    virtual void removeFsTreeEntry(const DriveKey& driveKey, const RemoveFilesystemEntryRequest& request) {
        const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
        for (auto& key : m_drives[driveKey].m_driveRequest.m_fullReplicatorList) {
            auto replicator = getReplicator(key);
            if (replicator) {
                replicator->removeFsTreeEntry(driveKey, request);
            }
        }
    }

    virtual void createDirectories(const DriveKey& driveKey, const CreateDirectoriesRequest& request) {
        const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
        for (auto& key : m_drives[driveKey].m_driveRequest.m_fullReplicatorList) {
            auto replicator = getReplicator(key);
            if (replicator) {
                replicator->createDirectories(driveKey, request);
            }
        }
    }

    virtual void folderIteratorCreate(const DriveKey& driveKey, const FolderIteratorCreateRequest& request) {
        const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
        for (auto& key : m_drives[driveKey].m_driveRequest.m_fullReplicatorList) {
            auto replicator = getReplicator(key);
            if (replicator) {
                replicator->folderIteratorCreate(driveKey, request);
            }
        }
    }

    virtual void folderIteratorDestroy(const DriveKey& driveKey, const FolderIteratorDestroyRequest& request) {
        const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
        for (auto& key : m_drives[driveKey].m_driveRequest.m_fullReplicatorList) {
            auto replicator = getReplicator(key);
            if (replicator) {
                replicator->folderIteratorDestroy(driveKey, request);
            }
        }
    }

    virtual void folderIteratorHasNext(const DriveKey& driveKey, const FolderIteratorHasNextRequest& request) {
        const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
        for (auto& key : m_drives[driveKey].m_driveRequest.m_fullReplicatorList) {
            auto replicator = getReplicator(key);
            if (replicator) {
                replicator->folderIteratorHasNext(driveKey, request);
            }
        }
    }

    virtual void folderIteratorNext(const DriveKey& driveKey, const FolderIteratorNextRequest& request) {
        const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
        for (auto& key : m_drives[driveKey].m_driveRequest.m_fullReplicatorList) {
            auto replicator = getReplicator(key);
            if (replicator) {
                replicator->folderIteratorNext(driveKey, request);
            }
        }
    }

    virtual void moveFsTreeEntry(const DriveKey& driveKey, const MoveFilesystemEntryRequest& request) {
        const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
        for (auto& key : m_drives[driveKey].m_driveRequest.m_fullReplicatorList) {
            auto replicator = getReplicator(key);
            if (replicator) {
                replicator->moveFsTreeEntry(driveKey, request);
            }
        }
    }

    virtual void isFile(const DriveKey& driveKey, const PathIsFileRequest& request) {
        const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
        for (auto& key : m_drives[driveKey].m_driveRequest.m_fullReplicatorList) {
            auto replicator = getReplicator(key);
            if (replicator) {
                replicator->pathIsFile(driveKey, request);
            }
        }
    }

    virtual void getSize(const DriveKey& driveKey, const FileSizeRequest& request) {
        const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
        for (auto& key : m_drives[driveKey].m_driveRequest.m_fullReplicatorList) {
            auto replicator = getReplicator(key);
            if (replicator) {
                replicator->fileSize(driveKey, request);
            }
        }
    }

    virtual void pathExist(const DriveKey& driveKey, const PathExistRequest& request) {
        const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
        for (auto& key : m_drives[driveKey].m_driveRequest.m_fullReplicatorList) {
            auto replicator = getReplicator(key);
            if (replicator) {
                replicator->pathExist(driveKey, request);
            }
        }
    }

    virtual void
    applySandboxManualModifications(const DriveKey& driveKey, const ApplySandboxModificationsRequest& request) {
        const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
        for (auto& key : m_drives[driveKey].m_driveRequest.m_fullReplicatorList) {
            auto replicator = getReplicator(key);
            if (replicator) {
                replicator->applySandboxManualModifications(driveKey, request);
            }
        }
    }

    virtual void evaluateStorageHash(const DriveKey& driveKey, const EvaluateStorageHashRequest& request) {
        const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
        for (auto& key : m_drives[driveKey].m_driveRequest.m_fullReplicatorList) {
            auto replicator = getReplicator(key);
            if (replicator) {
                replicator->evaluateStorageHash(driveKey, request);
            }
        }
    }

    virtual void
    applyStorageManualModifications(const DriveKey& driveKey, const ApplyStorageModificationsRequest& request) {
        const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
        for (auto& key : m_drives[driveKey].m_driveRequest.m_fullReplicatorList) {
            auto replicator = getReplicator(key);
            if (replicator) {
                replicator->applyStorageManualModifications(driveKey, request);
            }
        }
    }

    virtual void manualSynchronize(const DriveKey& driveKey, const SynchronizationRequest& request) {
        const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
        for (auto& key : m_drives[driveKey].m_driveRequest.m_fullReplicatorList) {
            auto replicator = getReplicator(key);
            if (replicator) {
                replicator->manualSynchronize(driveKey, request);
            }
        }
    }

    virtual void getAbsolutePath(const DriveKey& driveKey, const FileInfoRequest& request) {
        const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
        for (auto& key : m_drives[driveKey].m_driveRequest.m_fullReplicatorList) {
            auto replicator = getReplicator(key);
            if (replicator) {
                replicator->getFileInfo( driveKey, request );
            }
        }
    }

    virtual void getFilesystem(const DriveKey& driveKey, const FilesystemRequest& request) {
        const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
        for (auto& key : m_drives[driveKey].m_driveRequest.m_fullReplicatorList) {
            auto replicator = getReplicator(key);
            if (replicator) {
                replicator->getFilesystem(driveKey, request);
            }
        }
    }

    virtual void downloadFromDrive(const Key& driveKey, const DownloadRequest& request) {
        for (auto& replicator : m_replicators) {
            if (replicator) {
                replicator->asyncAddDownloadChannelInfo(driveKey, std::make_unique<DownloadRequest>(request));
            }
        }
    }

    virtual void closeDrive(const Key& driveKey) {
        auto transactionHash = randomByteArray<Hash256>();
        for (auto& key : m_drives[driveKey].m_driveRequest.m_fullReplicatorList) {
            auto replicator = getReplicator(key);
            if (replicator) {
                replicator->asyncCloseDrive(driveKey, transactionHash);
            }
        }
    }

    virtual void closeDownloadChannel(const Key& channelId) {
        auto transactionHash = randomByteArray<Hash256>();
        auto channelHash = *reinterpret_cast<const Hash256*>(&channelId);
        for (auto& replicator : m_replicators) {
            if (replicator) {
                replicator->asyncInitiateDownloadApprovalTransactionInfo(transactionHash, channelHash);
            }
        }
    }

    virtual void cancelModification(const Key& driveKey, const Hash256& transactionHash) {
        const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);

        m_drives[driveKey].m_driveRequest.m_completedModifications.push_back({transactionHash, CompletedModification::CompletedModificationStatus::CANCELLED});

        std::erase_if(m_drives[driveKey].m_pendingModifications, [&](const auto& item) {
            return item.m_transactionHash == transactionHash;
        });
        for (auto& key : m_drives[driveKey].m_driveRequest.m_fullReplicatorList) {
            auto replicator = getReplicator(key);
            if (replicator) {
                replicator->asyncCancelModify(driveKey, transactionHash);
            }
        }
    }

    virtual void startVerification(const Key& driveKey, const VerificationRequest& request) {
        const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
        auto& drive = m_drives[driveKey];
        ASSERT_FALSE(drive.m_pendingVerification);
        drive.m_pendingVerification = request;
        for (auto& key : drive.m_driveRequest.m_fullReplicatorList) {
            auto replicator = getReplicator(key);
            if (replicator) {
                replicator->asyncStartDriveVerification(driveKey, std::make_unique<VerificationRequest>(request));
            }
        }
    }

    virtual void cancelVerification(const Key& driveKey, const Hash256& request) {
        const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
        auto& drive = m_drives[driveKey];
        if (!drive.m_pendingVerification || drive.m_pendingVerification->m_tx != request) {
            return;
        }
        drive.m_pendingVerification.reset();
        for (auto& key : m_drives[driveKey].m_driveRequest.m_fullReplicatorList) {
            auto replicator = getReplicator(key);
            {
                replicator->asyncCancelDriveVerification(driveKey);
            }
        }
    }

#ifdef __APPLE__
#pragma mark--ReplicatorEventHandler methods and variables
#endif

    // It will be called before 'replicator' shuts down
    virtual void willBeTerminated(Replicator& replicator) override {
    }

    virtual void downloadApprovalTransactionIsReady(Replicator& replicator,
                                                    const DownloadApprovalTransactionInfo& info) override {
        const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
        if (!m_dnApprovalTransactionInfo) {
            m_dnApprovalTransactionInfo = {info};
            for (auto& r : m_replicators) {
                if (r) {
                    r->asyncDownloadApprovalTransactionHasBeenPublished(m_dnApprovalTransactionInfo->m_blockHash,
                                                                        m_dnApprovalTransactionInfo->m_downloadChannelId,
                                                                        true);
                }
            }
            m_downloadApprovedCondVar.notify_all();
        }
    }

    virtual void verificationTransactionIsReady(Replicator& replicator,
                                                const VerifyApprovalTxInfo& info) override {
        const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
        auto& drive = m_drives[info.m_driveKey];
        if (!drive.m_pendingVerification || drive.m_pendingVerification->m_tx != info.m_tx) {
            return;
        }
        drive.m_pendingVerification.reset();
        if (!m_verifyApprovalTransactionInfo.contains(info.m_tx)) {
            m_verifyApprovalTransactionInfo[info.m_tx] = info;
            for (auto& r : m_replicators) {
                if (r) {
                    r->asyncVerifyApprovalTransactionHasBeenPublished(info);
                }
            }
            m_verificationCondVar.notify_all();
        }
    }

    // It will be called when rootHash is calculated in sandbox
    virtual void rootHashIsCalculated(Replicator& replicator,
                                      const sirius::Key& driveKey,
                                      const sirius::drive::InfoHash& modifyTransactionHash,
                                      const sirius::drive::InfoHash& sandboxRootHash) override {
        EXLOG("rootHashIsCalculated: " << replicator.dbgReplicatorName());
        std::unique_lock<std::mutex> lock(m_rootHashCalculatedMutex);
        m_rootHashCalculatedCounters[modifyTransactionHash]++;
        m_rootHashCalculatedCondVars[modifyTransactionHash].notify_all();
    }

    void driveIsInitialized(Replicator& replicator, const Key& driveKey, const InfoHash& rootHash) override {
        EXLOG("DriveIsInitialized: " << replicator.dbgReplicatorName());
        std::unique_lock<std::mutex> lock(m_driveIsInitializedMutex);
        m_driveIsInitializedCounters[driveKey]++;
        m_driveIsInitializedCondVars[driveKey].notify_all();
    }

public:
    // It will be called when transaction could not be completed
    virtual void modifyTransactionEndedWithError(Replicator& replicator,
                                                 const sirius::Key& driveKey,
                                                 const ModificationRequest& modifyRequest,
                                                 const std::string& reason,
                                                 int errorCode) override {}

    // It will initiate the approving of modify transaction
    virtual void
    modifyApprovalTransactionIsReady(Replicator& replicator, const ApprovalTransactionInfo& transactionInfo) override {
        EXLOG("modifyApprovalTransactionIsReady: " << replicator.dbgReplicatorName());
        const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);

        auto& pendingModifications = m_drives[transactionInfo.m_driveKey].m_pendingModifications;

        if (!pendingModifications.empty() && pendingModifications.front().m_transactionHash == transactionInfo.m_modifyTransactionHash) {

            const auto& replicators = m_drives[transactionInfo.m_driveKey].m_driveRequest.m_fullReplicatorList;
            for (const auto& opinion : transactionInfo.m_opinions) {
                if (std::find(replicators.begin(), replicators.end(), opinion.m_replicatorKey) == replicators.end()) {
                    EXLOG("Fault Replicator Opinion")
                    replicator.asyncApprovalTransactionHasFailedInvalidOpinions(
                        transactionInfo.m_driveKey, transactionInfo.m_modifyTransactionHash);
                    return;
                }
            }

            if (transactionInfo.m_opinions.size() <= (2 * replicators.size()) / 3) {
                EXLOG("Fault Opinions Number")
                replicator.asyncApprovalTransactionHasFailedInvalidOpinions(
                    transactionInfo.m_driveKey, transactionInfo.m_modifyTransactionHash);
                return;
            }

            EXLOG(toString(transactionInfo.m_modifyTransactionHash));

            for (const auto& opinion : transactionInfo.m_opinions) {
                std::cout << " key:" << int(opinion.m_replicatorKey[0]) << " ";
                for (size_t i = 0; i < opinion.m_uploadLayout.size(); i++) {
                    std::cout << int(opinion.m_uploadLayout[i].m_key[0]) << ":"
                              << opinion.m_uploadLayout[i].m_uploadedBytes << " ";
                }
            }

            m_drives[transactionInfo.m_driveKey].m_driveRequest.m_expectedCumulativeDownloadSize += m_drives[transactionInfo.m_driveKey].m_pendingModifications.front().m_maxDataSize;
            m_drives[transactionInfo.m_driveKey].m_pendingModifications.pop_front();

            auto& drive = m_drives[transactionInfo.m_driveKey];

            for (const auto& opinion : transactionInfo.m_opinions) {
                auto it = drive.m_uploads.find(opinion.m_replicatorKey);

                if (it != drive.m_uploads.end()) {
                    const auto& initialOpinions = opinion.m_uploadLayout;
                    for (const auto& [key, bytes] : initialOpinions) {
                        auto replicatorKey = key;
                        auto opinionIt = std::find_if(it->second.begin(),
                                                      it->second.end(),
                                                      [&](const auto& item) { return item.m_key == replicatorKey; });

                        if (opinionIt != it->second.end()) {
                            ASSERT_LE(opinionIt->m_uploadedBytes, bytes);
                        }
                    }
                }
                drive.m_uploads[opinion.m_replicatorKey] = opinion.m_uploadLayout;
            }

            m_drives[transactionInfo.m_driveKey].m_lastApprovedModification = transactionInfo;
            m_rootHashes[m_drives[transactionInfo.m_driveKey].m_lastApprovedModification->m_modifyTransactionHash] = transactionInfo.m_rootHash;
            for (auto& key : m_drives[transactionInfo.m_driveKey].m_driveRequest.m_fullReplicatorList) {
                if (auto r = getReplicator(key); r) {
                    r->asyncApprovalTransactionHasBeenPublished(
                        std::make_unique<PublishedModificationApprovalTransactionInfo>(transactionInfo));
                }
            }

            for (const auto& opinion : transactionInfo.m_opinions) {
                auto size =
                    std::accumulate(opinion.m_uploadLayout.begin(),
                                    opinion.m_uploadLayout.end(),
                                    0UL,
                                    [](const auto& sum, const auto& item) {
                                        return sum + item.m_uploadedBytes;
                                    });
                m_modificationSizes[transactionInfo.m_modifyTransactionHash].insert(size);
            }

            ASSERT_EQ(m_modificationSizes[transactionInfo.m_modifyTransactionHash].size(), 1);
        }
    }

    virtual void singleModifyApprovalTransactionIsReady(Replicator& replicator,
                                                        const ApprovalTransactionInfo& transactionInfo) override {
        const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);

        const auto& replicators = m_drives[transactionInfo.m_driveKey].m_driveRequest.m_fullReplicatorList;

        if (std::find(replicators.begin(), replicators.end(), transactionInfo.m_opinions[0].m_replicatorKey) == replicators.end()) {
            EXLOG("Fault Single Opinion")
            return;
        }

        if (transactionInfo.m_modifyTransactionHash == m_drives[transactionInfo.m_driveKey].m_lastApprovedModification->m_modifyTransactionHash) {
            EXLOG("modifySingleApprovalTransactionIsReady: " << replicator.dbgReplicatorName()
                                                             << " "
                                                             << toString(transactionInfo.m_modifyTransactionHash));

            auto& drive = m_drives[transactionInfo.m_driveKey];
            for (const auto& opinion : transactionInfo.m_opinions) {
                auto it = drive.m_uploads.find(opinion.m_replicatorKey);

                if (it != drive.m_uploads.end()) {
                    const auto& initialOpinions = opinion.m_uploadLayout;
                    for (const auto& [key, bytes] : initialOpinions) {
                        auto replicatorKey = key;
                        auto opinionIt = std::find_if(it->second.begin(),
                                                      it->second.end(),
                                                      [&](const auto& item) { return item.m_key == replicatorKey; });

                        if (opinionIt != it->second.end()) {
                            ASSERT_LE(opinionIt->m_uploadedBytes, bytes);
                        }
                    }
                }
                drive.m_uploads[opinion.m_replicatorKey] = opinion.m_uploadLayout;
            }

            ASSERT_EQ(transactionInfo.m_opinions.size(), 1);

            {
                std::ostringstream str;
                str << "sizes before";
                for (const auto& size : m_modificationSizes[transactionInfo.m_modifyTransactionHash]) {
                    str << " " << size;
                }
                EXLOG(str.str());
            }

            for (const auto& opinion : transactionInfo.m_opinions) {
                auto size =
                    std::accumulate(opinion.m_uploadLayout.begin(),
                                    opinion.m_uploadLayout.end(),
                                    0UL,
                                    [](const auto& sum, const auto& item) {
                                        return sum + item.m_uploadedBytes;
                                    });
                m_modificationSizes[transactionInfo.m_modifyTransactionHash].insert(size);
            }
            {
                std::ostringstream str;
                str << "sizes after";
                for (const auto& size : m_modificationSizes[transactionInfo.m_modifyTransactionHash]) {
                    str << " " << size;
                }
                EXLOG(str.str());
            }
            ASSERT_EQ(m_modificationSizes[transactionInfo.m_modifyTransactionHash].size(), 1);

            EXLOG("Single Size " << *m_modificationSizes[transactionInfo.m_modifyTransactionHash].begin());

            replicator.asyncSingleApprovalTransactionHasBeenPublished(
                    std::make_unique<PublishedModificationSingleApprovalTransactionInfo>(transactionInfo));
        }
    };

    // It will be called after the drive is synchronized with sandbox
    virtual void driveModificationIsCompleted(Replicator& replicator,
                                              const sirius::Key& driveKey,
                                              const sirius::drive::InfoHash& modifyTransactionHash,
                                              const sirius::drive::InfoHash& rootHash) override {
        EXLOG("Completed modification " << replicator.dbgReplicatorName());
        std::unique_lock<std::mutex> lock(modifyCompleteMutex);
        modifyCompleteCounters[modifyTransactionHash]++;
        modifyCompleteCondVars[modifyTransactionHash].notify_all();
    }

    void driveIsClosed(Replicator& replicator, const Key& driveKey, const Hash256& transactionHash) override {
        std::unique_lock<std::mutex> lock(driveClosedMutex);
        EXLOG("driveIsClosed: " << replicator.dbgReplicatorName());
        driveClosedCounter++;
        driveClosedCondVar.notify_all();
    }

    void driveModificationIsCanceled(Replicator& replicator, const Key& driveKey,
                                     const Hash256& modifyTransactionHash) override {
        EXLOG("modificationIsCanceled: " << replicator.dbgReplicatorName());
    }

    void opinionHasBeenReceived(Replicator& replicator, const ApprovalTransactionInfo& info) override {
        replicator.asyncOnOpinionReceived(info);
    }

    void downloadOpinionHasBeenReceived(Replicator& replicator, const DownloadApprovalTransactionInfo& info) override {
        replicator.asyncOnDownloadOpinionReceived(std::make_unique<DownloadApprovalTransactionInfo>(info));
    }

    void waitDriveIsInitialized(const Key& driveKey, int number) {
        std::unique_lock<std::mutex> lock(m_rootHashCalculatedMutex);
        m_driveIsInitializedCondVars[driveKey].wait(lock, [this, driveKey, number] {
            return m_driveIsInitializedCounters[driveKey] == number;
        });
    }

    void waitRootHashCalculated(const Hash256& modification, int number) {
        std::unique_lock<std::mutex> lock(m_rootHashCalculatedMutex);
        m_rootHashCalculatedCondVars[modification].wait(lock, [this, modification, number] {
            return m_rootHashCalculatedCounters[modification] == number;
        });
    }

    void waitModificationEnd(const Hash256& modification, int number) {
        std::unique_lock<std::mutex> lock(modifyCompleteMutex);
        modifyCompleteCondVars[modification].wait(lock, [this, modification, number] {
            return modifyCompleteCounters[modification] == number;
        });
    }

    void waitDriveClosure() {
        std::unique_lock<std::mutex> lock(driveClosedMutex);
        driveClosedCondVar.wait(lock, [this] {
            return driveClosedCounter == m_replicators.size();
        });
    }

    void waitDownloadApproval() {
        std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
        m_downloadApprovedCondVar.wait(lock, [this] {
            return m_dnApprovalTransactionInfo;
        });
    }

    void waitVerificationApproval(const Hash256& tx) {
        std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
        m_verificationCondVar.wait(lock, [=, this] {
            return m_verifyApprovalTransactionInfo.contains(tx);
        });
    }

public:
    std::shared_ptr<Replicator> getReplicator(const Key& replicator) {
        auto it = std::find_if(m_replicators.begin(), m_replicators.end(), [&](const auto& item) {
            return item && item->dbgReplicatorKey() == replicator;
        });

        if (it == m_replicators.end()) {
            return {};
        }
        return *it;
    }
};
} // namespace sirius::drive::test

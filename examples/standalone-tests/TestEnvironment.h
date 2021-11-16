#pragma once
#include "types.h"
#include "drive/Session.h"
#include "drive/ClientSession.h"
#include "drive/Replicator.h"
#include "drive/FlatDrive.h"
#include "drive/FsTree.h"
#include "drive/Utils.h"
#include "crypto/Signer.h"
#include "utils.h"
#include <fstream>
#include <filesystem>
#include <future>
#include <condition_variable>
#include "boost/date_time/posix_time/posix_time.hpp"

#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/kademlia/ed25519.hpp>

#include <sirius_drive/session_delegate.h>

namespace sirius::drive::test {

    inline std::string intToString02(int n) {
        char str[10];
        snprintf(str, 7, "%02d", n);
        return str;
    }

    class TestEnvironment : public ReplicatorEventHandler {
    public:

        std::vector<std::shared_ptr<Replicator>> m_replicators;
        std::vector<std::shared_ptr<sirius::crypto::KeyPair>> m_keys;
        ReplicatorList m_addrList;
        std::vector<std::thread> m_threads;

        std::condition_variable modifyCompleteCondVar;
        std::atomic<unsigned int> modifyCompleteCounter{0};
        std::mutex modifyCompleteMutex;

        std::condition_variable driveClosedCondVar;
        std::atomic<unsigned int> driveClosedCounter{0};
        std::mutex driveClosedMutex;

        std::optional<ApprovalTransactionInfo> m_approvalTransactionInfo;
        std::optional<DownloadApprovalTransactionInfo> m_dnApprovalTransactionInfo;
        std::mutex m_transactionInfoMutex;

    public:
        TestEnvironment(int numberOfReplicators,
                        std::string ipAddr0,
                        int port0,
                        std::string rootFolder0,
                        std::string sandboxRootFolder0,
                        bool useTcpSocket,
                        int modifyApprovalDelay,
                        int downloadApprovalDelay,
                        bool startReplicator = true) {
            if (true) {
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

                    //EXLOG( "creating: " << dbgReplicatorName << " with key: " <<  int(replicatorKeyPair.publicKey().array()[0]) );

                    boost::asio::ip::tcp::endpoint point = {boost::asio::ip::address::from_string(ipAddr),
                                                            static_cast<ushort>(port)};

                    auto replicator = createDefaultReplicator(
                            *m_keys.back(),
                            std::move(ipAddr),
                            std::to_string(port),
                            std::move(rootFolder),
                            std::move(sandboxRootFolder),
                            useTcpSocket,
                            (ReplicatorEventHandler &) *this,
                            dbgReplicatorName.c_str());

                    replicator->setDownloadApprovalTransactionTimerDelay(modifyApprovalDelay);
                    replicator->setModifyApprovalTransactionTimerDelay(downloadApprovalDelay);

                    if (startReplicator)
                        replicator->start();

                    m_replicators.emplace_back(replicator);

                    m_addrList.emplace_back(ReplicatorInfo{point, m_keys.back()->publicKey()});
                    m_threads.push_back({});
                }
            }
        }

        void addDrive(const Key &driveKey, uint64_t driveSize) {
            for (auto &replicator: m_replicators) {
                auto result = replicator->addDrive(driveKey, driveSize, m_addrList);
                assert(result.empty());
            }
        }

        void modifyDrive(const Key &driveKey, const ModifyRequest &request) {
            for (auto &replicator: m_replicators) {
                std::thread([replicator, driveKey, request] {
                    auto result = replicator->modify(driveKey, ModifyRequest(request));
                    assert(result.empty());
                }).detach();
            }
        }

        void downloadFromDrive(const std::array<uint8_t,32>&   channelKey,
                               size_t                          prepaidDownloadSize,
                               const Key&                      driveKey,
                               const std::vector<Key>&         clients) {
            for (auto &replicator: m_replicators) {
                std::thread([=, this] {
                    replicator->addDownloadChannelInfo(channelKey,
                                                       prepaidDownloadSize,
                                                       driveKey,
                                                       m_addrList,
                                                       clients);
                }).detach();
            }
        }

        void closeDrive(const Key& driveKey) {
            auto transactionHash = randomByteArray<Hash256>();
            for (auto &replicator: m_replicators) {
                std::thread([replicator, driveKey, transactionHash] {
                    replicator->removeDrive(driveKey, transactionHash);
                }).detach();
            }
        }

        void cancelModification(const Key& driveKey, const Hash256& transactionHash) {
            for (auto &replicator: m_replicators) {
                std::thread([replicator, driveKey, transactionHash] {
                    replicator->cancelModify(driveKey, transactionHash);
                }).detach();
            }
        }

#pragma mark --ReplicatorEventHandler methods and variables

// It will be called before 'replicator' shuts down
        virtual void willBeTerminated(Replicator &replicator) override {

        }

        virtual void downloadApprovalTransactionIsReady(Replicator &replicator,
                                                        const DownloadApprovalTransactionInfo &info) override {
            std::cout << "downloadApproved" << std::endl;
            const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
            if ( !m_dnApprovalTransactionInfo )
            {
                m_dnApprovalTransactionInfo = { std::move(info) };
                for (auto &r: m_replicators) {
                    std::thread([=, this] {
                        r->onDownloadApprovalTransactionHasBeenPublished(m_dnApprovalTransactionInfo->m_blockHash,
                                                                         m_dnApprovalTransactionInfo->m_downloadChannelId,
                                                                         true);
                    }).detach();
                }
            }
        }

// It will be called when rootHash is calculated in sandbox
        virtual void rootHashIsCalculated(Replicator &replicator,
                                          const sirius::Key &driveKey,
                                          const sirius::drive::InfoHash &modifyTransactionHash,
                                          const sirius::drive::InfoHash &sandboxRootHash) override {
            EXLOG("rootHashIsCalculated: " << replicator.dbgReplicatorName());
        }

        // It will be called when transaction could not be completed
        virtual void modifyTransactionEndedWithError( Replicator& replicator,
                                                      const sirius::Key& driveKey,
                                                      const ModifyRequest& modifyRequest,
                                                      const std::string& reason,
                                                      int errorCode ) override
        {}

        // It will initiate the approving of modify transaction
        virtual void
        modifyApprovalTransactionIsReady(Replicator &replicator, ApprovalTransactionInfo &&transactionInfo) override {
            EXLOG("modifyApprovalTransactionIsReady: " << replicator.dbgReplicatorName());
            const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);

            for (const auto &opinion: transactionInfo.m_opinions) {
                std::cout << " key:" << int(opinion.m_replicatorKey[0]) << " ";
                for (size_t i = 0; i < opinion.m_uploadReplicatorKeys.size(); i += 32) {
                    std::cout << int(opinion.m_uploadReplicatorKeys[i]) << ":"
                    << opinion.m_replicatorUploadBytes[i / 32] << " ";
                }
                std::cout << "client:" << opinion.m_clientUploadBytes << std::endl;
            }

            if (!m_approvalTransactionInfo) {
                m_approvalTransactionInfo = {std::move(transactionInfo)};

                for (const auto &r: m_replicators) {
                    std::thread([r, this] {
                        r->onApprovalTransactionHasBeenPublished(*m_approvalTransactionInfo);
                    }).detach();
                }
            }
        }

        // It will initiate the approving of single modify transaction
        virtual void singleModifyApprovalTransactionIsReady(Replicator &replicator,
                                                            ApprovalTransactionInfo &&transactionInfo) override {
            EXLOG("modifySingleApprovalTransactionIsReady: " << replicator.dbgReplicatorName());
            auto approval = transactionInfo;
            std::thread([&replicator, approval] {
                replicator.onSingleApprovalTransactionHasBeenPublished(approval);
            }).detach();
        };

        // It will be called after the drive is synchronized with sandbox
        virtual void driveModificationIsCompleted(Replicator &replicator,
                                                  const sirius::Key &driveKey,
                                                  const sirius::drive::InfoHash &modifyTransactionHash,
                                                  const sirius::drive::InfoHash &rootHash) override {
            EXLOG("Completed modification " << replicator.dbgReplicatorName());
            modifyCompleteCounter++;
            modifyCompleteCondVar.notify_all();
        }

        void driveIsClosed(Replicator &replicator, const Key &driveKey, const Hash256 &transactionHash) override {
            EXLOG("driveIsClosed: " << replicator.dbgReplicatorName());
            driveClosedCounter++;
            driveClosedCondVar.notify_all();
        }

        void driveModificationIsCanceled(Replicator &replicator, const Key &driveKey,
                                         const Hash256 &modifyTransactionHash) override {
            EXLOG("modificationIsCanceled: " << replicator.dbgReplicatorName());

        }

        void waitModificationEnd() {
            std::unique_lock<std::mutex> lock(modifyCompleteMutex);
            modifyCompleteCondVar.wait(lock, [this] {
                return modifyCompleteCounter == m_replicators.size();
            });
        }

        void waitDriveClosure() {
            std::unique_lock<std::mutex> lock(driveClosedMutex);
            driveClosedCondVar.wait(lock, [this] {
                return driveClosedCounter == m_replicators.size() + 1;
            });
        }
    };
}
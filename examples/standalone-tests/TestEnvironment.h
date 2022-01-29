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
#include <numeric>

#include "gtest/gtest.h"

namespace sirius::drive::test {

    inline std::string intToString02(int n) {
        char str[10];
        snprintf(str, 7, "%02d", n);
        return str;
    }

    class TestEnvironment : public ReplicatorEventHandler, DbgReplicatorEventHandler {
    public:

        std::vector<std::shared_ptr<sirius::crypto::KeyPair>> m_keys;
        ReplicatorList m_addrList;
        std::vector<ReplicatorInfo> m_bootstraps;

        std::map<Hash256, std::condition_variable> modifyCompleteCondVars;
        std::map<Hash256, int> modifyCompleteCounters;
        std::mutex modifyCompleteMutex;

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

        std::deque<ModificationRequest> m_pendingModifications;
        std::optional<ApprovalTransactionInfo> m_lastApprovedModification;
        std::map<Hash256, InfoHash> m_rootHashes;

        std::map<Key, std::set<uint64_t>> m_modificationSizes;

        std::optional<std::pair<Key, AddDriveRequest>> drive;

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
                        int startReplicator = -1) {
            if ( startReplicator == -1 )
            {
                startReplicator = numberOfReplicators;
            }
            for (int i = 1; i <= numberOfReplicators; i++)
            {
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
                if ( i == 1 )
                {
                    m_bootstraps = { { { boost::asio::ip::make_address(ipAddr), (unsigned short) port },
                                       m_keys.back()->publicKey() } };
                }

                //EXLOG( "creating: " << dbgReplicatorName << " with key: " <<  int(replicatorKeyPair.publicKey().array()[0]) );

                m_rootFolders.push_back(rootFolder);
                m_sandboxFolders.push_back(sandboxRootFolder);

                if (i <= startReplicator)
                {
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

                    replicator->setDownloadApprovalTransactionTimerDelay(modifyApprovalDelay);
                    replicator->setModifyApprovalTransactionTimerDelay(downloadApprovalDelay);
                    replicator->start();
                    m_replicators.emplace_back(replicator);
                }
                else {
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
                             int downloadApprovalDelay)
        {
            const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
            if ( !m_replicators[i - 1] )
            {
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
                replicator->asyncAddDrive(drive->first, drive->second);
                if (m_lastApprovedModification)
                {
                    replicator->asyncApprovalTransactionHasBeenPublished(*m_lastApprovedModification);
                }
                for (const auto& modification: m_pendingModifications)
                {
                    replicator->asyncModify(drive->first, modification);
                }
            }
        }

        void stopReplicator(int i)
        {
            m_replicators[i - 1].reset();
        }

        virtual ~TestEnvironment()
        {}

        virtual void addDrive(const Key &driveKey, const Key& client, uint64_t driveSize) {
            drive = {driveKey, {driveSize, 0, m_addrList, client, m_addrList, m_addrList}};
            for (auto &replicator: m_replicators) {
                if ( replicator )
                {
                    replicator->asyncAddDrive(drive->first, drive->second);
                }
            }
        }

        virtual void modifyDrive(const Key &driveKey, const ModificationRequest &request) {
            const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
            m_pendingModifications.push_back(request);
            for (auto &replicator: m_replicators) {
                if ( replicator )
                {
                    replicator->asyncModify(driveKey, ModificationRequest(request));
                }
            }
        }

        virtual void downloadFromDrive(const Key& driveKey, const DownloadRequest& request) {
            for (auto &replicator: m_replicators) {
                if ( replicator )
                {
                    replicator->asyncAddDownloadChannelInfo(driveKey, DownloadRequest(request));
                }
            }
        }

        virtual void closeDrive(const Key& driveKey) {
            auto transactionHash = randomByteArray<Hash256>();
            for (auto &replicator: m_replicators) {
                if ( replicator )
                {
                    replicator->asyncCloseDrive(driveKey, transactionHash);
                }
            }
        }

        virtual void closeDownloadChannel( const Key& channelId )
        {
            auto transactionHash = randomByteArray<Hash256>();
            auto channelHash = *reinterpret_cast<const Hash256 *>(&channelId);
            for (auto &replicator: m_replicators) {
                if ( replicator )
                {
                    replicator->asyncInitiateDownloadApprovalTransactionInfo(transactionHash, channelHash);
                }
            }
        }

        virtual void cancelModification(const Key& driveKey, const Hash256& transactionHash) {
            const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
            std::erase_if(m_pendingModifications, [&] (const auto& item) {
                return item.m_transactionHash == transactionHash;
            });
            for (auto &replicator: m_replicators) {
                if ( replicator )
                {
                    replicator->asyncCancelModify(driveKey, transactionHash);
                }
            }
        }

        virtual void startVerification(const Key& driveKey, const VerificationRequest& request)
        {
            for (auto &r: m_replicators) {
                if ( r )
                {
                    r->asyncStartDriveVerification(driveKey, request);
                }
            }
        }

        virtual void cancelVerification(const Key& driveKey, const Hash256& request)
        {
            for (auto &r: m_replicators) {
                if ( r )
                {
                    r->asyncCancelDriveVerification(driveKey, request);
                }
            }
        }

#ifdef __APPLE__
#pragma mark --ReplicatorEventHandler methods and variables
#endif

// It will be called before 'replicator' shuts down
        virtual void willBeTerminated(Replicator &replicator) override {

        }

        virtual void downloadApprovalTransactionIsReady(Replicator &replicator,
                                                        const DownloadApprovalTransactionInfo &info) override {
            const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
            if ( !m_dnApprovalTransactionInfo )
            {
                m_dnApprovalTransactionInfo = { info };
                for (auto &r: m_replicators) {
                    if ( r )
                    {
                        r->asyncDownloadApprovalTransactionHasBeenPublished(m_dnApprovalTransactionInfo->m_blockHash,
                                                                            m_dnApprovalTransactionInfo->m_downloadChannelId,
                                                                            true);
                    }
                }
                m_downloadApprovedCondVar.notify_all();
            }
        }

        virtual void verificationTransactionIsReady(Replicator &replicator,
                                                        const VerifyApprovalTxInfo &info) override {
            const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
            if ( !m_verifyApprovalTransactionInfo.contains(info.m_tx) )
            {
                m_verifyApprovalTransactionInfo[info.m_tx] = info;
                for (auto &r: m_replicators) {
                    if ( r )
                    {
                        r->asyncVerifyApprovalTransactionHasBeenPublished( info );
                    }
                }
                m_verificationCondVar.notify_all();
            }
        }

// It will be called when rootHash is calculated in sandbox
        virtual void rootHashIsCalculated(Replicator &replicator,
                                          const sirius::Key &driveKey,
                                          const sirius::drive::InfoHash &modifyTransactionHash,
                                          const sirius::drive::InfoHash &sandboxRootHash) override {
            EXLOG("rootHashIsCalculated: " << replicator.dbgReplicatorName());
            std::unique_lock<std::mutex> lock(m_rootHashCalculatedMutex);
            m_rootHashCalculatedCounters[modifyTransactionHash]++;
            m_rootHashCalculatedCondVars[modifyTransactionHash].notify_all();
        }

        // It will be called when transaction could not be completed
        virtual void modifyTransactionEndedWithError( Replicator& replicator,
                                                      const sirius::Key& driveKey,
                                                      const ModificationRequest& modifyRequest,
                                                      const std::string& reason,
                                                      int errorCode ) override
        {}

        // It will initiate the approving of modify transaction
        virtual void
        modifyApprovalTransactionIsReady(Replicator &replicator, const ApprovalTransactionInfo &transactionInfo) override {
            EXLOG("modifyApprovalTransactionIsReady: " << replicator.dbgReplicatorName());
            const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);

            if (m_pendingModifications.front().m_transactionHash == transactionInfo.m_modifyTransactionHash) {

                EXLOG( toString(transactionInfo.m_modifyTransactionHash) );

                for (const auto &opinion: transactionInfo.m_opinions) {
                    std::cout << " key:" << int(opinion.m_replicatorKey[0]) << " ";
                    for (size_t i = 0; i < opinion.m_uploadLayout.size(); i++) {
                        std::cout << int(opinion.m_uploadLayout[i].m_key[0]) << ":"
                        << opinion.m_uploadLayout[i].m_uploadedBytes << " ";
                    }
                    std::cout << "client:" << opinion.m_clientUploadBytes << std::endl;
                }

                drive->second.m_expectedCumulativeDownloadSize += m_pendingModifications.front().m_maxDataSize;
                m_pendingModifications.pop_front();
                m_lastApprovedModification = transactionInfo;
                m_rootHashes[m_lastApprovedModification->m_modifyTransactionHash] = transactionInfo.m_rootHash;
                for (const auto &r: m_replicators) {
                    if ( r )
                    {
                        r->asyncApprovalTransactionHasBeenPublished(
                                ApprovalTransactionInfo(transactionInfo));
                    }
                }

                for (const auto& opinion: transactionInfo.m_opinions)
                {
                    auto size =
                            std::accumulate(opinion.m_uploadLayout.begin(),
                                            opinion.m_uploadLayout.end(),
                                            opinion.m_clientUploadBytes,
                                            [] (const auto& sum, const auto& item) {
                                return sum + item.m_uploadedBytes;
                            });
                    m_modificationSizes[transactionInfo.m_modifyTransactionHash].insert(size);
                }

                ASSERT_EQ(m_modificationSizes[transactionInfo.m_modifyTransactionHash].size(), 1);
            }
        }

        virtual void singleModifyApprovalTransactionIsReady(Replicator &replicator,
                                                            const ApprovalTransactionInfo &transactionInfo) override {
            const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
            if (transactionInfo.m_modifyTransactionHash == m_lastApprovedModification->m_modifyTransactionHash)
            {
                EXLOG("modifySingleApprovalTransactionIsReady: " << replicator.dbgReplicatorName()
                << " " << toString(transactionInfo.m_modifyTransactionHash) );
                replicator.asyncSingleApprovalTransactionHasBeenPublished(transactionInfo);

                ASSERT_EQ(transactionInfo.m_opinions.size(), 1);

                {
                    std::ostringstream str;
                    str << "sizes before" ;
                    for ( const auto& size: m_modificationSizes[transactionInfo.m_modifyTransactionHash] )
                    {
                        str << " " << size;
                    }
                    EXLOG( str.str() );
                }

                for (const auto& opinion: transactionInfo.m_opinions) {
                    auto size =
                            std::accumulate(opinion.m_uploadLayout.begin(),
                                            opinion.m_uploadLayout.end(),
                                            opinion.m_clientUploadBytes,
                                            [] (const auto& sum, const auto& item) {
                                return sum + item.m_uploadedBytes;
                            });
                    m_modificationSizes[transactionInfo.m_modifyTransactionHash].insert(size);
                }
                {
                    std::ostringstream str;
                    str << "sizes after" ;
                    for ( const auto& size: m_modificationSizes[transactionInfo.m_modifyTransactionHash] )
                    {
                        str << " " << size;
                    }
                    EXLOG( str.str() );
                }
                ASSERT_EQ(m_modificationSizes[transactionInfo.m_modifyTransactionHash].size(), 1);
            }
        };

        // It will be called after the drive is synchronized with sandbox
        virtual void driveModificationIsCompleted(Replicator &replicator,
                                                  const sirius::Key &driveKey,
                                                  const sirius::drive::InfoHash &modifyTransactionHash,
                                                  const sirius::drive::InfoHash &rootHash) override {
            EXLOG("Completed modification " << replicator.dbgReplicatorName());
            std::unique_lock<std::mutex> lock(modifyCompleteMutex);
            modifyCompleteCounters[modifyTransactionHash] ++;
            modifyCompleteCondVars[modifyTransactionHash].notify_all();
        }

        void driveIsClosed(Replicator &replicator, const Key &driveKey, const Hash256 &transactionHash) override {
            std::unique_lock<std::mutex> lock(driveClosedMutex);
            EXLOG("driveIsClosed: " << replicator.dbgReplicatorName());
            driveClosedCounter++;
            driveClosedCondVar.notify_all();
        }

        void driveModificationIsCanceled(Replicator &replicator, const Key &driveKey,
                                         const Hash256 &modifyTransactionHash) override {
            EXLOG("modificationIsCanceled: " << replicator.dbgReplicatorName());

        }

        void opinionHasBeenReceived(Replicator &replicator, const ApprovalTransactionInfo &info) override
        {
            replicator.asyncOnOpinionReceived(info);
        }

        void downloadOpinionHasBeenReceived(Replicator &replicator, const DownloadApprovalTransactionInfo &info) override
        {
            replicator.asyncOnDownloadOpinionReceived(info);
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
    };
}

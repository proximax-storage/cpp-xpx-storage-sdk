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

    std::string intToString02(int n);

    class TestEnvironment : public ReplicatorEventHandler {
    public:

        std::vector<std::shared_ptr<Replicator>> m_replicators;
        ReplicatorList m_addrList;
        std::vector<std::thread> m_threads;

        std::condition_variable modifyCompleteCondVar;
        std::atomic<unsigned int> modifyCompleteCounter{0};
        std::mutex modifyCompleteMutex;

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
                        bool startReplicator = true);

        void addDrive(const Key &driveKey, uint64_t driveSize);

        void modifyDrive(const Key &driveKey, const ModifyRequest &request);

#pragma mark --ReplicatorEventHandler methods and variables

// It will be called before 'replicator' shuts down
        virtual void willBeTerminated(Replicator &replicator) override;

        virtual void downloadApprovalTransactionIsReady(Replicator &replicator,
                                                        const DownloadApprovalTransactionInfo &info) override;

// It will be called when rootHash is calculated in sandbox
        virtual void rootHashIsCalculated(Replicator &replicator,
                                          const sirius::Key &driveKey,
                                          const sirius::drive::InfoHash &modifyTransactionHash,
                                          const sirius::drive::InfoHash &sandboxRootHash) override;

        // It will be called when transaction could not be completed
        virtual void modifyTransactionEndedWithError(Replicator &replicator,
                                                 const sirius::Key &driveKey,
                                                 const ModifyRequest& modifyRequest,
                                                 const std::string &reason,
                                                 int errorCode) override;

        // It will initiate the approving of modify transaction
        virtual void
        modifyApprovalTransactionIsReady(Replicator &replicator, ApprovalTransactionInfo &&transactionInfo) override;

        // It will initiate the approving of single modify transaction
        virtual void singleModifyApprovalTransactionIsReady(Replicator &replicator,
                                                            ApprovalTransactionInfo &&transactionInfo) override;

        // It will be called after the drive is synchronized with sandbox
        virtual void driveModificationIsCompleted(Replicator &replicator,
                                                  const sirius::Key &driveKey,
                                                  const sirius::drive::InfoHash &modifyTransactionHash,
                                                  const sirius::drive::InfoHash &rootHash) override;

        void waitModificationEnd();
    };
}
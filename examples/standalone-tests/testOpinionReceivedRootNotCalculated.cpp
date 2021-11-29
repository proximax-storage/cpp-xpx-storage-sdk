#include <drive/ExtensionEmulator.h>
#include <numeric>
#include <set>
#include "TestEnvironment.h"
#include "utils.h"
#include "gtest/gtest.h"


#include "types.h"
#include "drive/Session.h"
#include "drive/Replicator.h"
#include "drive/FlatDrive.h"
#include "drive/Utils.h"

namespace sirius::drive::test {

/// change this macro for your test
#define TEST_NAME OpinionReceivedRootNotCalculated

#define ENVIRONMENT_CLASS JOIN(TEST_NAME, TestEnvironment)

    namespace {
        class ENVIRONMENT_CLASS : public TestEnvironment {
        public:
            ENVIRONMENT_CLASS(
                    int numberOfReplicators,
                    const std::string &ipAddr0,
                    int port0,
                    const std::string &rootFolder0,
                    const std::string &sandboxRootFolder0,
                    bool useTcpSocket,
                    int modifyApprovalDelay,
                    int downloadApprovalDelay,
                    int backDownloadRate,
                    bool startReplicator = true)
                    : TestEnvironment(
                    numberOfReplicators,
                    ipAddr0,
                    port0,
                    rootFolder0,
                    sandboxRootFolder0,
                    useTcpSocket,
                    modifyApprovalDelay,
                    downloadApprovalDelay,
                    startReplicator) {
                lt::settings_pack pack;
                pack.set_int(lt::settings_pack::download_rate_limit, backDownloadRate);
                m_replicators.back()->setSessionSettings(pack, true);
            }

            void modifyApprovalTransactionIsReady(Replicator &replicator,
                                                  ApprovalTransactionInfo &&transactionInfo) override {
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

                if (m_pendingModifications.front().array() != transactionInfo.m_modifyTransactionHash)
                {
                    return;
                }

                if (replicator.keyPair().publicKey() == m_replicators.back()->replicatorKey())
                {

                    ASSERT_EQ(transactionInfo.m_opinions.size(), m_replicators.size());

                    std::set<uint64_t> sizes;
                    for (const auto& opinion: transactionInfo.m_opinions) {
                        auto size =
                                std::accumulate(opinion.m_replicatorUploadBytes.begin(),
                                                opinion.m_replicatorUploadBytes.end(),
                                                opinion.m_clientUploadBytes);
                        sizes.insert(size);
                    }

                    ASSERT_EQ(sizes.size(), 1);

                    m_pendingModifications.pop_front();
                    m_lastApprovedModification = transactionInfo.m_modifyTransactionHash;

                    for (const auto &r: m_replicators) {
                        std::thread([r, transactionInfo] {
                            r->asyncApprovalTransactionHasBeenPublished(transactionInfo);
                        }).detach();
                    }
                }
            }
        };
    }

    TEST(ModificationTest, TEST_NAME) {
        fs::remove_all(ROOT_FOLDER);

        auto startTime = std::clock();

        lt::settings_pack pack;
        TestClient client(pack);

        EXLOG("");

        ENVIRONMENT_CLASS env(
                NUMBER_OF_REPLICATORS, REPLICATOR_ADDRESS, PORT, DRIVE_ROOT_FOLDER,
                SANDBOX_ROOT_FOLDER, USE_TCP, 1, 1, 100 * 1024 * 1024);

        EXLOG("\n# Client started: 1-st upload");
        auto actionList = createActionList(CLIENT_WORK_FOLDER);
        client.modifyDrive(actionList, env.m_addrList);

        env.addDrive(DRIVE_PUB_KEY, 100 * 1024 * 1024);
        env.modifyDrive(DRIVE_PUB_KEY, {client.m_actionListHashes.back(),
                                        client.m_modificationTransactionHashes.back(),
                                        BIG_FILE_SIZE + 1024,
                                        env.m_addrList,
                                        client.m_clientKeyPair.publicKey()});

        EXLOG("\ntotal time: " << float(std::clock() - startTime) / CLOCKS_PER_SEC);
        std::thread([] {
            std::this_thread::sleep_for(std::chrono::seconds(60));
            ASSERT_EQ(true, false);
        }).detach();
        env.waitModificationEnd(client.m_modificationTransactionHashes.back(), NUMBER_OF_REPLICATORS);
    }

#undef TEST_NAME
}

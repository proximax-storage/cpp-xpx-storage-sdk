#include <drive/ExtensionEmulator.h>
#include <set>
#include <numeric>
#include "TestEnvironment.h"
#include "utils.h"

#include "types.h"
#include "drive/Session.h"
#include "drive/ClientSession.h"
#include "drive/Replicator.h"
#include "drive/FlatDrive.h"
#include "drive/FsTree.h"
#include "drive/Utils.h"
#include "crypto/Signer.h"
#include "gtest/gtest.h"

using namespace sirius::drive::test;

namespace sirius::drive::test {

/// change this macro for your test
#define TEST_NAME SlowClientModification

#define ENVIRONMENT_CLASS JOIN(TEST_NAME, TestEnvironment)
#define RUN_TEST void JOIN(run, TEST_NAME)()

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
                        startReplicator)
        {}

        void
        modifyApprovalTransactionIsReady(Replicator &replicator, ApprovalTransactionInfo &&transactionInfo) override {
            std::set<uint64_t> sizes;
            for (const auto& opinion: transactionInfo.m_opinions) {
                auto size =
                        std::accumulate(opinion.m_replicatorUploadBytes.begin(),
                                        opinion.m_replicatorUploadBytes.end(),
                                        opinion.m_clientUploadBytes);
                sizes.insert(size);
            }

            ASSERT_EQ(sizes.size(), 1);
            TestEnvironment::modifyApprovalTransactionIsReady(replicator, std::move(transactionInfo));
        }

        void singleModifyApprovalTransactionIsReady(Replicator &replicator,
                                                    ApprovalTransactionInfo &&transactionInfo) override {
            TestEnvironment::singleModifyApprovalTransactionIsReady(replicator, std::move(transactionInfo));
        }
    };

    TEST(ModificationTest, TEST_NAME) {
        fs::remove_all(ROOT_FOLDER);

        auto startTime = std::clock();

        lt::settings_pack pack;
//        pack.set_int(lt::settings_pack::upload_rate_limit, 1024 * 1024);
        TestClient client(pack);

        EXLOG("");

        ENVIRONMENT_CLASS env(
                NUMBER_OF_REPLICATORS, REPLICATOR_ADDRESS, PORT, DRIVE_ROOT_FOLDER,
                SANDBOX_ROOT_FOLDER, USE_TCP, 1, 1);

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
            std::this_thread::sleep_for(std::chrono::seconds(120));
            ASSERT_EQ(true, false);
        }).detach();
        env.waitModificationEnd(client.m_modificationTransactionHashes.back(), NUMBER_OF_REPLICATORS);
    }

#undef TEST_NAME
}

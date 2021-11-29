#include <drive/ExtensionEmulator.h>
#include "TestEnvironment.h"
#include "utils.h"
#include <set>
#include <numeric>

#include "types.h"
#include "drive/Session.h"
#include "drive/Replicator.h"
#include "drive/FlatDrive.h"
#include "drive/Utils.h"
#include "gtest/gtest.h"

using namespace sirius::drive::test;

namespace sirius::drive::test {

    /// change this macro for your test
#define TEST_NAME ApprovalReceivedRootCalculated

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
                startReplicator) {}

        void
        modifyApprovalTransactionIsReady(Replicator &replicator, ApprovalTransactionInfo &&transactionInfo) override {
            m_ignoredReplicator = transactionInfo.m_opinions.back().m_replicatorKey;
            transactionInfo.m_opinions.pop_back();

            TestEnvironment::modifyApprovalTransactionIsReady(replicator, ApprovalTransactionInfo(transactionInfo));
            ASSERT_EQ(transactionInfo.m_opinions.size(), m_replicators.size() - 1);
            for (const auto& opinion: transactionInfo.m_opinions) {
                auto size =
                        std::accumulate(opinion.m_uploadLayout.begin(),
                                        opinion.m_uploadLayout.end(),
                                        opinion.m_clientUploadBytes,
                                        [] (const auto& sum, const auto& item) {
                            return sum + item.m_uploadedBytes;
                        });
                m_modificationSizes.insert(size);
            }

            ASSERT_EQ(m_modificationSizes.size(), 1);
        }

        void singleModifyApprovalTransactionIsReady(Replicator &replicator,
                                                            ApprovalTransactionInfo &&transactionInfo) override {
            TestEnvironment::singleModifyApprovalTransactionIsReady(replicator, std::move(transactionInfo));
            ASSERT_EQ(replicator.keyPair().publicKey(), m_ignoredReplicator);

            const auto& opinion = transactionInfo.m_opinions.front();
            auto size =
                    std::accumulate(opinion.m_uploadLayout.begin(),
                                    opinion.m_uploadLayout.end(),
                                    opinion.m_clientUploadBytes,
                                    [] (const auto& sum, const auto& item) {
                        return sum + item.m_uploadedBytes;
                    });
            m_modificationSizes.insert(size);

            ASSERT_EQ(m_modificationSizes.size(), 1);
        };

        std::array<uint8_t,32> m_ignoredReplicator;
        std::set<uint64_t> m_modificationSizes;
    };

    TEST(ModificationTest, TEST_NAME) {
        fs::remove_all(ROOT_FOLDER);

        auto startTime = std::clock();

        lt::settings_pack pack;
        TestClient client(pack);

        EXLOG("");

        ENVIRONMENT_CLASS env(
                NUMBER_OF_REPLICATORS, REPLICATOR_ADDRESS, PORT, DRIVE_ROOT_FOLDER,
                SANDBOX_ROOT_FOLDER, USE_TCP, 10000, 10000);

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
        env.waitModificationEnd(client.m_modificationTransactionHashes.back(), NUMBER_OF_REPLICATORS);
    }

#undef TEST_NAME
}

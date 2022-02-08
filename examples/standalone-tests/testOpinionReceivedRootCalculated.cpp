#include "TestEnvironment.h"
#include "utils.h"
#include <set>
#include <numeric>
#include "gtest/gtest.h"

#include "types.h"
#include "../../src/drive/Session.h"
#include "drive/Replicator.h"
#include "drive/FlatDrive.h"
#include "drive/Utils.h"

using namespace sirius::drive::test;

/// change this macro for your test
#define TEST_NAME OpinionReceivedRootCalculated

#define ENVIRONMENT_CLASS JOIN(TEST_NAME, TestEnvironment)
#define RUN_TEST void JOIN(run, TEST_NAME)()

namespace sirius::drive::test
{
    class ENVIRONMENT_CLASS : public TestEnvironment
    {
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
                int minimalDownloadSpeed,
                int startReplicator = -1)
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
        {
            lt::settings_pack pack;
            for (uint i = 0; i < m_replicators.size(); i++)
            {
                pack.set_int(lt::settings_pack::download_rate_limit, (i + 1u) * minimalDownloadSpeed);
                m_replicators[i]->setSessionSettings(pack, true);
            }
        }

        void
        modifyApprovalTransactionIsReady(Replicator &replicator, const ApprovalTransactionInfo &transactionInfo) override
        {
            EXLOG("modifyApprovalTransactionIsReady: " << replicator.dbgReplicatorName());
            const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);

            for (const auto &opinion: transactionInfo.m_opinions)
            {
                std::cout << " key:" << int(opinion.m_replicatorKey[0]) << " ";
                for (size_t i = 0; i < opinion.m_uploadLayout.size(); i++)
                {
                    std::cout << int(opinion.m_uploadLayout[i].m_key[0]) << ":"
                              << opinion.m_uploadLayout[i].m_uploadedBytes << " ";
                }
            }

            if (m_drives[DRIVE_PUB_KEY].m_pendingModifications.front().m_transactionHash != transactionInfo.m_modifyTransactionHash)
            {
                return;
            }

            if (replicator.dbgReplicatorKey() == m_replicators.back()->dbgReplicatorKey())
            {
                ASSERT_EQ(transactionInfo.m_opinions.size(), m_replicators.size());

                std::set<uint64_t> sizes;
                for (const auto &opinion: transactionInfo.m_opinions)
                {
                    auto size =
                            std::accumulate(opinion.m_uploadLayout.begin(),
                                            opinion.m_uploadLayout.end(),
                                            0,
                                            [](const auto &sum, const auto &item)
                                            {
                                                return sum + item.m_uploadedBytes;
                                            });
                    sizes.insert(size);
                }

                ASSERT_EQ(sizes.size(), 1);

                m_drives[transactionInfo.m_driveKey].m_pendingModifications.pop_front();
                m_drives[transactionInfo.m_driveKey].m_lastApprovedModification = transactionInfo;

                for (const auto &r: m_replicators)
                {
                    r->asyncApprovalTransactionHasBeenPublished(
                        PublishedModificationApprovalTransactionInfo( transactionInfo ));
                }
            }
        }
    };

    TEST(ModificationTest, TEST_NAME)
    {
        fs::remove_all(ROOT_FOLDER);

        auto startTime = std::clock();

        EXLOG("");

        ENVIRONMENT_CLASS env(
                NUMBER_OF_REPLICATORS, REPLICATOR_ADDRESS, PORT, DRIVE_ROOT_FOLDER,
                SANDBOX_ROOT_FOLDER, USE_TCP, 20000, 20000, 1024 * 1024);

        lt::settings_pack pack;
        endpoint_list bootstraps;
        for ( const auto& b: env.m_bootstraps )
        {
            bootstraps.push_back( b.m_endpoint );
        }
        TestClient client(bootstraps, pack);

        EXLOG("\n# Client started: 1-st upload");
        auto actionList = createActionList(CLIENT_WORK_FOLDER);
        client.modifyDrive(actionList, env.m_addrList, DRIVE_PUB_KEY);

        env.addDrive(DRIVE_PUB_KEY, client.m_clientKeyPair.publicKey(), 100 * 1024 * 1024);
        env.modifyDrive(DRIVE_PUB_KEY, {client.m_actionListHashes.back(),
                                        client.m_modificationTransactionHashes.back(),
                                        BIG_FILE_SIZE + 1024,
                                        env.m_addrList });

        EXLOG("\ntotal time: " << float(std::clock() - startTime) / CLOCKS_PER_SEC);
        std::thread([]
                    {
                        std::this_thread::sleep_for(std::chrono::seconds(60));
                        ASSERT_EQ(true, false);
                    }).detach();
        env.waitModificationEnd(client.m_modificationTransactionHashes.back(), NUMBER_OF_REPLICATORS);
    }

#undef TEST_NAME
}

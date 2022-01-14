#include <emulator/ExtensionEmulator.h>
#include <numeric>
#include "TestEnvironment.h"
#include "utils.h"

#include "types.h"
#include "drive/Session.h"
#include "drive/Utils.h"

using namespace sirius::drive::test;

namespace sirius::drive::test
{

    /// change this macro for your test
#define TEST_NAME ThreeModificationsAvoidSingleApproval

#define ENVIRONMENT_CLASS JOIN(TEST_NAME, TestEnvironment)

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
                int backDownloadRateLimit,
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
            pack.set_int(lt::settings_pack::download_rate_limit, backDownloadRateLimit);
            m_replicators.back()->setSessionSettings(pack, true);
        }

        virtual void
        modifyApprovalTransactionIsReady(Replicator &replicator, ApprovalTransactionInfo &&transactionInfo) override
        {
            const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);

            if (m_pendingModifications.front().m_transactionHash == transactionInfo.m_modifyTransactionHash)
            {
                EXLOG("modifyApprovalTransactionIsReady: " << replicator.dbgReplicatorName());
                m_expectingApproval = transactionInfo;

                drive->second.m_expectedCumulativeDownloadSize += m_pendingModifications.front().m_maxDataSize;
                m_pendingModifications.pop_front();
                m_lastApprovedModification = transactionInfo;
                m_rootHashes[m_lastApprovedModification->m_modifyTransactionHash] = transactionInfo.m_rootHash;
                if (transactionInfo.m_modifyTransactionHash == m_forbiddenTransaction[0].array())
                {
                    approveModification();
                }
            }
        }

        void approveModification()
        {
            auto transactionInfo = *m_expectingApproval;
            m_expectingApproval.reset();
            EXLOG(toString(transactionInfo.m_modifyTransactionHash));

            for (const auto &opinion: transactionInfo.m_opinions)
            {
                std::cout << " key:" << int(opinion.m_replicatorKey[0]) << " ";
                for (size_t i = 0; i < opinion.m_uploadLayout.size(); i++)
                {
                    std::cout << int(opinion.m_uploadLayout[i].m_key[0]) << ":"
                              << opinion.m_uploadLayout[i].m_uploadedBytes << " ";
                }
                std::cout << "client:" << opinion.m_clientUploadBytes << std::endl;
            }

            for (uint i = 0; i < m_replicators.size(); i++)
            {
                const auto &r = m_replicators[i];
                if (r)
                {
                    r->asyncApprovalTransactionHasBeenPublished(
                            ApprovalTransactionInfo(transactionInfo));
                }
            }

            for (const auto &opinion: transactionInfo.m_opinions)
            {
                auto size =
                        std::accumulate(opinion.m_uploadLayout.begin(),
                                        opinion.m_uploadLayout.end(),
                                        opinion.m_clientUploadBytes,
                                        [](const auto &sum, const auto &item)
                                        {
                                            return sum + item.m_uploadedBytes;
                                        });
                m_modificationSizes[transactionInfo.m_modifyTransactionHash].insert(size);
            }

            ASSERT_EQ(m_modificationSizes[transactionInfo.m_modifyTransactionHash].size(), 1);
        }

        virtual void singleModifyApprovalTransactionIsReady(Replicator &replicator,
                                                            ApprovalTransactionInfo &&transactionInfo) override
        {
            EXLOG("single " << toString(transactionInfo.m_modifyTransactionHash))
            if (auto it =
                        std::find(m_forbiddenTransaction.begin(),
                                  m_forbiddenTransaction.end(),
                                  transactionInfo.m_modifyTransactionHash); it != m_forbiddenTransaction.end())
            {
                const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
                ASSERT_EQ(m_expectingApproval.has_value(), true);
                approveModification();
            } else
            {
                TestEnvironment::singleModifyApprovalTransactionIsReady(replicator,
                                                                        ApprovalTransactionInfo(transactionInfo));
            }
        };

        std::vector<Hash256> m_forbiddenTransaction;
        std::optional<ApprovalTransactionInfo> m_expectingApproval;
    };

    TEST(ModificationTest, TEST_NAME)
    {
        fs::remove_all(ROOT_FOLDER);

        EXLOG("");


        ENVIRONMENT_CLASS env(
                NUMBER_OF_REPLICATORS, REPLICATOR_ADDRESS, PORT, DRIVE_ROOT_FOLDER,
                SANDBOX_ROOT_FOLDER, USE_TCP, 1, 1, 1024 * 1024);

        lt::settings_pack pack;
        pack.set_int(lt::settings_pack::upload_rate_limit, 0);
        endpoint_list bootstraps;
        for ( const auto& b: env.m_bootstraps )
        {
            bootstraps.push_back( b.m_endpoint );
        }
        TestClient client(bootstraps, pack);

        EXLOG("\n# Client started: 1-st upload");
        client.modifyDrive(createActionList(CLIENT_WORK_FOLDER), env.m_addrList);
        client.modifyDrive(createActionList_2(CLIENT_WORK_FOLDER), env.m_addrList);
        client.modifyDrive(createActionList_3(CLIENT_WORK_FOLDER), env.m_addrList);

        env.m_forbiddenTransaction.emplace_back(client.m_modificationTransactionHashes[0]);
        env.m_forbiddenTransaction.emplace_back(client.m_modificationTransactionHashes[1]);

        env.addDrive(DRIVE_PUB_KEY, client.m_clientKeyPair.publicKey(), 100 * 1024 * 1024);
        env.modifyDrive(DRIVE_PUB_KEY, {client.m_actionListHashes[0],
                                        client.m_modificationTransactionHashes[0],
                                        BIG_FILE_SIZE + 1024,
                                        env.m_addrList,
                                        client.m_clientKeyPair.publicKey()});

        EXLOG("Required modification " << toString(client.m_modificationTransactionHashes[0]));

        env.modifyDrive(DRIVE_PUB_KEY, {client.m_actionListHashes[1],
                                        client.m_modificationTransactionHashes[1],
                                        BIG_FILE_SIZE + 1024,
                                        env.m_addrList,
                                        client.m_clientKeyPair.publicKey()});

        EXLOG("Required modification " << toString(client.m_modificationTransactionHashes[1]));

        env.modifyDrive(DRIVE_PUB_KEY, {client.m_actionListHashes[2],
                                        client.m_modificationTransactionHashes[2],
                                        BIG_FILE_SIZE + 1024,
                                        env.m_addrList,
                                        client.m_clientKeyPair.publicKey()});

        EXLOG("Required modification " << toString(client.m_modificationTransactionHashes[2]));

        env.waitModificationEnd(client.m_modificationTransactionHashes[2], NUMBER_OF_REPLICATORS);
    }

#undef TEST_NAME
}
#include "TestEnvironment.h"
#include "utils.h"
#include <set>

#include "types.h"

#include "drive/Replicator.h"
#include "drive/FlatDrive.h"
#include "drive/Utils.h"
#include "gtest/gtest.h"

using namespace sirius::drive::test;

namespace sirius::drive::test
{

    /// change this macro for your test
#define TEST_NAME OfflineReplicatorCancelModification

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
                int downloadRate,
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
                startReplicator),
                m_offlineWork(boost::asio::make_work_guard(m_offlineContext))
        {
            for (const auto &replicator: m_replicators)
            {
                lt::settings_pack pack;
                pack.set_int(lt::settings_pack::download_rate_limit, downloadRate);
                replicator->setSessionSettings(pack, true);
            }
        }

        void runBack()
        {
            m_offlineThread = std::thread([this]
                                          {
                                              m_offlineContext.run();
                                          });
        }

        void modifyDrive(const Key &driveKey, const ModificationRequest &request) override
        {
            const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
            m_drives[driveKey].m_pendingModifications.push_back(request);
            for (uint i = 0; i < m_replicators.size() - 1; i++)
            {
                auto &replicator = m_replicators[i];
                replicator->asyncModify(driveKey, std::make_unique<ModificationRequest>(request));
            }
            boost::asio::post(m_offlineContext, [=, this]
                                  {
                m_replicators.back()->asyncModify(driveKey, std::make_unique<ModificationRequest>(request));
                                  });
        }

        void cancelModification(const Key &driveKey, const Hash256 &transactionHash) override
        {
            const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
            std::erase_if(m_drives[driveKey].m_pendingModifications, [&](const auto &item)
            {
                return item.m_transactionHash == transactionHash;
            });
            for (uint i = 0; i < m_replicators.size() - 1; i++)
            {
                auto &replicator = m_replicators[i];
                replicator->asyncCancelModify(driveKey, transactionHash);
            }
            boost::asio::post(m_offlineContext, [=, this]
                                  {
                                      m_replicators.back()->asyncCancelModify(driveKey, transactionHash);
                                  });
        }

        // It will initiate the approving of modify transaction
        void
        modifyApprovalTransactionIsReady(Replicator &replicator, const ApprovalTransactionInfo &transactionInfo) override
        {
            EXLOG("modifyApprovalTransactionIsReady: " << replicator.dbgReplicatorName());
            const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);

            if ( Hash256(transactionInfo.m_modifyTransactionHash) == m_cancelledModification )
            {
                return;
            }

            if (m_drives[transactionInfo.m_driveKey].m_pendingModifications.front().m_transactionHash == transactionInfo.m_modifyTransactionHash)
            {

                EXLOG(toString(transactionInfo.m_modifyTransactionHash));

                for (const auto &opinion: transactionInfo.m_opinions)
                {
                    std::cout << " key:" << int(opinion.m_replicatorKey[0]) << " ";
                    for (size_t i = 0; i < opinion.m_uploadLayout.size(); i++)
                    {
                        std::cout << int(opinion.m_uploadLayout[i].m_key[0]) << ":"
                                  << opinion.m_uploadLayout[i].m_uploadedBytes << " ";
                    }
                }

                for (const auto &opinion: transactionInfo.m_opinions)
                {
                    auto size =
                            std::accumulate(opinion.m_uploadLayout.begin(),
                                            opinion.m_uploadLayout.end(),
                                            0UL,
                                            [](const auto &sum, const auto &item)
                                            {
                                                return sum + item.m_uploadedBytes;
                                            });
                    m_modificationSizes[transactionInfo.m_modifyTransactionHash].insert(size);
                }

                ASSERT_EQ(m_modificationSizes[transactionInfo.m_modifyTransactionHash].size(), 1);

                m_drives[transactionInfo.m_driveKey].m_pendingModifications.pop_front();
                m_drives[transactionInfo.m_driveKey].m_lastApprovedModification = transactionInfo;
                m_rootHashes[m_drives[transactionInfo.m_driveKey].m_lastApprovedModification->m_modifyTransactionHash] = transactionInfo.m_rootHash;
                for (uint i = 0; i < m_replicators.size() - 1; i++)
                {
                    const auto &r = m_replicators[i];
                    r->asyncApprovalTransactionHasBeenPublished(
                        std::make_unique<PublishedModificationApprovalTransactionInfo>(transactionInfo));
                }
                boost::asio::post(m_offlineContext, [=, this]
                                      {
                                          m_replicators.back()->asyncApprovalTransactionHasBeenPublished(
                                              std::make_unique<PublishedModificationApprovalTransactionInfo>(transactionInfo));

                                      });
            }
        }

        void singleModifyApprovalTransactionIsReady(Replicator &replicator,
                                                    const ApprovalTransactionInfo & transactionInfo) override
        {
            const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
            if (transactionInfo.m_modifyTransactionHash == m_drives[transactionInfo.m_driveKey].m_lastApprovedModification->m_modifyTransactionHash)
            {
                EXLOG("modifySingleApprovalTransactionIsReady: " << replicator.dbgReplicatorName()
                                                                 << " "
                                                                 << toString(transactionInfo.m_modifyTransactionHash));

                ASSERT_EQ(transactionInfo.m_opinions.size(), 1);

                for (const auto &opinion: transactionInfo.m_opinions)
                {
                    auto size =
                            std::accumulate(opinion.m_uploadLayout.begin(),
                                            opinion.m_uploadLayout.end(),
                                            0UL,
                                            [](const auto &sum, const auto &item)
                                            {
                                                return sum + item.m_uploadedBytes;
                                            });
                    m_modificationSizes[transactionInfo.m_modifyTransactionHash].insert(size);
                }
                ASSERT_EQ(m_modificationSizes[transactionInfo.m_modifyTransactionHash].size(), 1);

                if (replicator.dbgReplicatorKey() != m_replicators.back()->dbgReplicatorKey())
                {
                    replicator.asyncSingleApprovalTransactionHasBeenPublished( std::make_unique<PublishedModificationSingleApprovalTransactionInfo>(transactionInfo) );
                } else
                {
                    boost::asio::post(m_offlineContext, [=, &replicator]
                                          {
                                                                                            replicator.asyncSingleApprovalTransactionHasBeenPublished(
                                                std::make_unique<PublishedModificationSingleApprovalTransactionInfo>( transactionInfo ) );

                                          });
                }
            }
        };

        ~ENVIRONMENT_CLASS() override
        {
            m_offlineContext.stop();
            m_offlineThread.join();
        }

        Hash256 m_cancelledModification;
        boost::asio::io_context m_offlineContext;
        boost::asio::executor_work_guard<boost::asio::io_context::executor_type> m_offlineWork;
        std::thread m_offlineThread;
    };

    TEST(ModificationTest, TEST_NAME)
    {
        fs::remove_all(ROOT_FOLDER);

        EXLOG("");

        ENVIRONMENT_CLASS env(
                NUMBER_OF_REPLICATORS, REPLICATOR_ADDRESS, PORT, DRIVE_ROOT_FOLDER,
                SANDBOX_ROOT_FOLDER, USE_TCP, 10000, 10000, 1024 * 1024 );

        lt::settings_pack pack;
        endpoint_list bootstraps;
        for ( const auto& b: env.m_bootstraps )
        {
            bootstraps.push_back( b.m_endpoint );
        }
        TestClient client(bootstraps, pack);

        EXLOG("\n# Client started: 1-st upload");
        auto actionList = createActionList(CLIENT_WORK_FOLDER);
        client.modifyDrive(createActionList(CLIENT_WORK_FOLDER), env.m_addrList, DRIVE_PUB_KEY);
        client.modifyDrive(createActionList_2(CLIENT_WORK_FOLDER), env.m_addrList, DRIVE_PUB_KEY);

        env.m_cancelledModification = client.m_modificationTransactionHashes[0];
        env.addDrive(DRIVE_PUB_KEY, client.m_clientKeyPair.publicKey(), 100 * 1024 * 1024);
        env.modifyDrive(DRIVE_PUB_KEY, {client.m_actionListHashes[0],
                                        client.m_modificationTransactionHashes[0],
                                        BIG_FILE_SIZE + 5 * 1024 * 1024,
                                        env.m_addrList });

        EXLOG("Required modification " << toString(client.m_modificationTransactionHashes[0]));

        env.cancelModification(DRIVE_PUB_KEY, client.m_modificationTransactionHashes[0]);

        env.modifyDrive(DRIVE_PUB_KEY, {client.m_actionListHashes[1],
                                        client.m_modificationTransactionHashes[1],
                                        BIG_FILE_SIZE + 5 * 1024 * 1024,
                                        env.m_addrList });

        EXLOG("Required modification " << toString(client.m_modificationTransactionHashes[1]));

        env.waitModificationEnd(client.m_modificationTransactionHashes[1], NUMBER_OF_REPLICATORS - 1);

        env.runBack();

        env.waitModificationEnd(client.m_modificationTransactionHashes[1], NUMBER_OF_REPLICATORS);

        ASSERT_EQ(env.modifyCompleteCounters[client.m_modificationTransactionHashes[0]], 0);
    }

#undef TEST_NAME
}

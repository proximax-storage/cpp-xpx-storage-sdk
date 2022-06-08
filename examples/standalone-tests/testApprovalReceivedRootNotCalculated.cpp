#include <gtest/gtest.h>
#include <numeric>
#include "TestEnvironment.h"
#include "utils.h"

#include "types.h"
#include "../../src/drive/Session.h"
#include "drive/Replicator.h"
#include "drive/FlatDrive.h"
#include "drive/Utils.h"

namespace sirius::drive::test
{

#define TEST_NAME ApprovalReceivedRootNotCalculated

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
                int backDownloadRate,
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
            pack.set_int(lt::settings_pack::download_rate_limit, backDownloadRate);
            m_replicators.back()->setSessionSettings(pack, true);
        }

        void
        modifyApprovalTransactionIsReady(Replicator &replicator, const ApprovalTransactionInfo &transactionInfo) override
        {
            TestEnvironment::modifyApprovalTransactionIsReady(replicator, ApprovalTransactionInfo(transactionInfo));

            ASSERT_EQ(transactionInfo.m_opinions.size(), m_replicators.size() - 1);
            auto it = std::find_if(transactionInfo.m_opinions.begin(), transactionInfo.m_opinions.end(),
                                   [this](const SingleOpinion &opinion)
                                   {
                                       return opinion.m_replicatorKey ==
                                              m_replicators.back()->dbgReplicatorKey().array();
                                   });
            ASSERT_EQ(it, transactionInfo.m_opinions.end());
        }

        virtual void singleModifyApprovalTransactionIsReady(Replicator &replicator,
                                                            const ApprovalTransactionInfo &transactionInfo) override
        {

            ASSERT_EQ(replicator.dbgReplicatorKey(), m_replicators.back()->dbgReplicatorKey());
            TestEnvironment::singleModifyApprovalTransactionIsReady(replicator, std::move(transactionInfo));
        }
    };

    TEST(ModificationTest, TEST_NAME)
    {
        fs::remove_all(ROOT_FOLDER);

        auto startTime = std::clock();

        EXLOG("");

        ENVIRONMENT_CLASS env(
                NUMBER_OF_REPLICATORS, REPLICATOR_ADDRESS, PORT, DRIVE_ROOT_FOLDER,
                SANDBOX_ROOT_FOLDER, USE_TCP, 1, 1, 1024 * 1024);

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
                                        BIG_FILE_SIZE + 1024 * 1024,
                                        env.m_addrList });

        EXLOG("Actionlist Hash" << toString(client.m_actionListHashes.back()))

        EXLOG("\ntotal time: " << float(std::clock() - startTime) / CLOCKS_PER_SEC);
        env.waitModificationEnd(client.m_modificationTransactionHashes.back(), NUMBER_OF_REPLICATORS);
    }

#undef TEST_NAME
}

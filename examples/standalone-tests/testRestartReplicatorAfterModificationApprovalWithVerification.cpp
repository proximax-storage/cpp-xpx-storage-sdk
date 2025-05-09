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
#define TEST_NAME RestartReplicatorAfterModificationApprovalWithVerification

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
                  m_downloadRate(downloadRate)
        {
            for (auto replicator: m_replicators)
            {
                if (replicator)
                {
                    lt::settings_pack pack;
                    pack.set_int(lt::settings_pack::download_rate_limit, m_downloadRate);
                    replicator->setSessionSettings(pack, true);
                }
            }
        }

        void startReplicator(int i,
                             std::string ipAddr0,
                             int port0,
                             std::string rootFolder0,
                             std::string sandboxRootFolder0,
                             bool useTcpSocket,
                             int modifyApprovalDelay,
                             int downloadApprovalDelay) override
        {
            TestEnvironment::startReplicator(i,
                                             ipAddr0,
                                             port0,
                                             rootFolder0,
                                             sandboxRootFolder0,
                                             useTcpSocket,
                                             modifyApprovalDelay,
                                             downloadApprovalDelay);

            lt::settings_pack pack;
            pack.set_int(lt::settings_pack::download_rate_limit, m_downloadRate);
            m_replicators[i - 1]->setSessionSettings(pack, true);
        }

        int m_downloadRate;
    };

    TEST(ModificationTest, TEST_NAME)
    {
        fs::remove_all(ROOT_FOLDER);

        EXLOG("");

        ENVIRONMENT_CLASS env(
                NUMBER_OF_REPLICATORS, REPLICATOR_ADDRESS, PORT, DRIVE_ROOT_FOLDER,
                SANDBOX_ROOT_FOLDER, USE_TCP, 10000, 10000, 1024 * 1024);

        lt::settings_pack pack;
        endpoint_list bootstraps;
        for ( const auto& b: env.m_bootstraps )
        {
            bootstraps.push_back( b.m_endpoint );
        }
        TestClient client(bootstraps, pack);

        EXLOG("\n# Client started: 1-st upload");
        client.modifyDrive(createActionList(CLIENT_WORK_FOLDER), env.m_addrList, DRIVE_PUB_KEY);
        client.modifyDrive(createActionList_2(CLIENT_WORK_FOLDER), env.m_addrList, DRIVE_PUB_KEY);
        client.modifyDrive(createActionList_3(CLIENT_WORK_FOLDER), env.m_addrList, DRIVE_PUB_KEY);

        env.addDrive(DRIVE_PUB_KEY, client.m_clientKeyPair.publicKey(), 100 * 1024 * 1024);
        env.modifyDrive(DRIVE_PUB_KEY, {client.m_actionListHashes[0],
                                        client.m_modificationTransactionHashes[0],
                                        BIG_FILE_SIZE + 1024 * 1024,
                                        env.m_addrList });

        env.waitModificationEnd(client.m_modificationTransactionHashes[0], NUMBER_OF_REPLICATORS);

        env.stopReplicator(NUMBER_OF_REPLICATORS);

        auto verification = randomByteArray<Hash256>();
        env.startVerification( DRIVE_PUB_KEY,
                               {
            verification,
            0,
            env.m_drives[DRIVE_PUB_KEY].m_lastApprovedModification->m_modifyTransactionHash,
            env.m_addrList,
            30 * 60 * 1000, {}} );

        env.startReplicator(NUMBER_OF_REPLICATORS,
                            REPLICATOR_ADDRESS, PORT, DRIVE_ROOT_FOLDER,
                            SANDBOX_ROOT_FOLDER, USE_TCP, 10000, 10000);

        env.waitVerificationApproval(verification);

        const auto& verify_tx = env.m_verifyApprovalTransactionInfo[verification];
        for ( const auto& opinion: verify_tx.m_opinions )
        {
            for ( const auto& res: opinion.m_opinions )
            {
                EXPECT_EQ(res, 1);
            }
        }
    }

#undef TEST_NAME
}

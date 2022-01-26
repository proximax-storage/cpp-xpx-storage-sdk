#include "TestEnvironment.h"
#include "utils.h"
#include "gtest/gtest.h"

#include "types.h"
#include "drive/Session.h"
#include "drive/Utils.h"

using namespace sirius::drive::test;

namespace sirius::drive::test
{

/// change this macro for your test
#define TEST_NAME CancelRootHashCalculated

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
                int downloadRateLimit,
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
            pack.set_int(lt::settings_pack::download_rate_limit, downloadRateLimit);
            for (auto &replicator: m_replicators)
            {
                replicator->setSessionSettings(pack, true);
            }
        }

        void modifyApprovalTransactionIsReady(Replicator& replicator,
                                                               const ApprovalTransactionInfo& transactionInfo) override
        {
            return;
        }
    };

    TEST(ModificationTest, TEST_NAME)
    {
        fs::remove_all(ROOT_FOLDER);

        EXLOG("");

        ENVIRONMENT_CLASS env(
                NUMBER_OF_REPLICATORS, REPLICATOR_ADDRESS, PORT, DRIVE_ROOT_FOLDER,
                SANDBOX_ROOT_FOLDER, USE_TCP, 1, 1, 2 * 1024 * 1024);

        lt::settings_pack pack;
        pack.set_int(lt::settings_pack::upload_rate_limit, 0);
        endpoint_list bootstraps;
        for ( const auto& b: env.m_bootstraps )
        {
            bootstraps.push_back( b.m_endpoint );
        }
        TestClient client(bootstraps, pack);

        client.modifyDrive(createActionList(CLIENT_WORK_FOLDER), env.m_addrList);
        client.modifyDrive(createActionList_2(CLIENT_WORK_FOLDER), env.m_addrList);

        env.addDrive(DRIVE_PUB_KEY, client.m_clientKeyPair.publicKey(), 100 * 1024 * 1024);
        env.modifyDrive(DRIVE_PUB_KEY, {client.m_actionListHashes[0],
                                        client.m_modificationTransactionHashes[0],
                                        BIG_FILE_SIZE + 1024,
                                        env.m_addrList,
                                        client.m_clientKeyPair.publicKey()});


        env.waitRootHashCalculated(client.m_modificationTransactionHashes[0], NUMBER_OF_REPLICATORS);

        env.cancelModification(DRIVE_PUB_KEY, client.m_modificationTransactionHashes[0]);
        std::this_thread::sleep_for(std::chrono::seconds(30));

        for ( uint i = 0; i < env.m_replicators.size(); i++ )
        {
            const auto& rootFolder = env.m_rootFolders[i];
            const auto& sandboxFolder = env.m_sandboxFolders[i];
            EXPECT_TRUE( fs::exists( fs::path(rootFolder) / toString(DRIVE_PUB_KEY) / "drive" ));
            EXPECT_TRUE( fs::is_empty( fs::path(rootFolder) / toString(DRIVE_PUB_KEY) / "drive" ));
            EXPECT_TRUE( fs::exists( fs::path(sandboxFolder) / toString(DRIVE_PUB_KEY)) );
            EXPECT_TRUE( fs::is_empty( fs::path(sandboxFolder) / toString(DRIVE_PUB_KEY) ));
        }
    }

#undef TEST_NAME
}



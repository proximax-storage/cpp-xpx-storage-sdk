#include <numeric>
#include "TestEnvironment.h"
#include "utils.h"

#include "types.h"

#include "drive/Utils.h"

using namespace sirius::drive::test;

namespace sirius::drive::test
{

    /// change this macro for your test
#define TEST_NAME ManyVerifications

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
            for ( const auto& r: m_replicators )
            {
                if ( r )
                {
                    r->setVerifyCodeTimerDelay(0);
                    r->setVerifyApprovalTransactionTimerDelay(10 * 1000);
                }
            }
        }
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
        client.modifyDrive(createActionList(CLIENT_WORK_FOLDER), env.m_addrList, DRIVE_PUB_KEY);

        env.addDrive(DRIVE_PUB_KEY, client.m_clientKeyPair.publicKey(), 100 * 1024 * 1024);
        env.modifyDrive(DRIVE_PUB_KEY, {client.m_actionListHashes[0],
                                        client.m_modificationTransactionHashes[0],
                                        BIG_FILE_SIZE + 1024 * 1024,
                                        env.m_addrList });

        env.waitModificationEnd(client.m_modificationTransactionHashes[0], NUMBER_OF_REPLICATORS);

        // Replicators should find each other
        sleep(5);

        for ( int i = 0; i < 10000; i++ )
        {
            auto verification = randomByteArray<Hash256>();
            env.startVerification( DRIVE_PUB_KEY,
                                   {
                verification,
                0,
                env.m_drives[DRIVE_PUB_KEY].m_lastApprovedModification->m_rootHash,
                env.m_addrList,
                10 * 60 * 1000, {}} );
            std::this_thread::sleep_for( std::chrono::milliseconds( rand() % 500 ) );
            env.cancelVerification( DRIVE_PUB_KEY, verification );
        }
    }

#undef TEST_NAME
}

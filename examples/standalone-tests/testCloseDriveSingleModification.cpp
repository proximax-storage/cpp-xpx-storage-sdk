#include "TestEnvironment.h"
#include "utils.h"
#include "gtest/gtest.h"

#include "types.h"

#include "drive/ClientSession.h"
#include "drive/Replicator.h"
#include "drive/FlatDrive.h"
#include "drive/FsTree.h"
#include "drive/Utils.h"
#include "crypto/Signer.h"

using namespace sirius::drive::test;

namespace sirius::drive::test
{

/// change this macro for your test
#define TEST_NAME CloseDriveSingleModification

#define ENVIRONMENT_CLASS JOIN(TEST_NAME, TestEnvironment)
#define RUN_TEST void JOIN(run, TEST_NAME)()

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
        {}
    };

    TEST(ModificationTest, TEST_NAME)
    {
        fs::remove_all(ROOT_FOLDER);

        auto startTime = std::clock();

        ENVIRONMENT_CLASS env(
                NUMBER_OF_REPLICATORS, REPLICATOR_ADDRESS, PORT, DRIVE_ROOT_FOLDER,
                SANDBOX_ROOT_FOLDER, USE_TCP, 1, 1);

        lt::settings_pack pack;
        pack.set_int(lt::settings_pack::upload_rate_limit, 0);
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

        EXLOG("\ntotal time: " << float(std::clock() - startTime) / CLOCKS_PER_SEC);
        env.waitModificationEnd(client.m_modificationTransactionHashes.back(), NUMBER_OF_REPLICATORS);

        EXLOG("\n# Client asked to close drive");

        std::thread([]
                    {
                        std::this_thread::sleep_for(std::chrono::seconds(10));
                        ASSERT_EQ(true, false);
                    }).detach();

        env.closeDrive(DRIVE_PUB_KEY);
        env.waitDriveClosure();
    }

#undef TEST_NAME
}

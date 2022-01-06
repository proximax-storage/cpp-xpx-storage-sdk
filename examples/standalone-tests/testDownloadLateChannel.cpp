#include <emulator/ExtensionEmulator.h>
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

namespace sirius::drive::test
{

    /// change this macro for your test
#define TEST_NAME DownloadLateChannel

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

        EXLOG("");


        ENVIRONMENT_CLASS env(
                NUMBER_OF_REPLICATORS, REPLICATOR_ADDRESS, PORT, DRIVE_ROOT_FOLDER,
                SANDBOX_ROOT_FOLDER, USE_TCP, 10000, 10000);

        lt::settings_pack pack;
        endpoint_list bootstraps;
        for ( const auto& b: env.m_bootstraps )
        {
            bootstraps.push_back( b.m_endpoint );
        }
        TestClient client(bootstraps, pack);

        EXLOG("\n# Client started: 1-st upload");
        auto actionList = createActionList(CLIENT_WORK_FOLDER);
        client.modifyDrive(actionList, env.m_addrList);

        env.addDrive(DRIVE_PUB_KEY, client.m_clientKeyPair.publicKey(), 100 * 1024 * 1024);
        env.modifyDrive(DRIVE_PUB_KEY, {client.m_actionListHashes.back(),
                                        client.m_modificationTransactionHashes.back(),
                                        BIG_FILE_SIZE + 1024,
                                        env.m_addrList,
                                        client.m_clientKeyPair.publicKey()});

        EXLOG("Requested Modification: " << (int) client.m_modificationTransactionHashes.back().array()[0]);
        env.waitModificationEnd(client.m_modificationTransactionHashes.back(), NUMBER_OF_REPLICATORS);

        auto downloadChannel = randomByteArray<Key>();
        EXLOG("Download Channel: " << (int) downloadChannel.array()[0]);
        client.downloadFromDrive(env.m_rootHashes[env.m_lastApprovedModification->m_modifyTransactionHash],
                                 downloadChannel, env.m_addrList);

        std::this_thread::sleep_for(std::chrono::minutes(5));

        ASSERT_EQ(client.m_downloadCompleted[env.m_rootHashes[env.m_lastApprovedModification->m_modifyTransactionHash]],
                  false);

        env.downloadFromDrive(DRIVE_PUB_KEY, DownloadRequest{
                downloadChannel,
                10000000,
                env.m_addrList,
                {client.m_clientKeyPair.publicKey()}
        });

        client.waitForDownloadComplete(env.m_rootHashes[env.m_lastApprovedModification->m_modifyTransactionHash]);
    }

#undef TEST_NAME
}

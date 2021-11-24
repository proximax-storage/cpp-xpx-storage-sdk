#include <drive/ExtensionEmulator.h>
#include "TestEnvironment.h"
#include "utils.h"
#include "gtest/gtest.h"

#include "types.h"
#include "drive/Session.h"
#include "drive/Utils.h"

using namespace sirius::drive::test;

namespace sirius::drive::test {

/// change this macro for your test
#define TEST_NAME CancelDuringModification

#define ENVIRONMENT_CLASS JOIN(TEST_NAME, TestEnvironment)

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
                int downloadRateLimit,
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
                        startReplicator)
        {
            lt::settings_pack pack;
            pack.set_int(lt::settings_pack::download_rate_limit, downloadRateLimit);
            for (auto& replicator: m_replicators) {
                replicator->setSessionSettings(pack, true);
            }
        }
    };

    TEST(ModificationTest, TEST_NAME){
        fs::remove_all(ROOT_FOLDER);

        lt::settings_pack pack;
        pack.set_int(lt::settings_pack::upload_rate_limit, 0);
        TestClient client(pack);

        _LOG("");

        ENVIRONMENT_CLASS env(
                NUMBER_OF_REPLICATORS, REPLICATOR_ADDRESS, PORT, DRIVE_ROOT_FOLDER,
                SANDBOX_ROOT_FOLDER, USE_TCP, 1, 1, 5 * 1024 * 1024);

        client.modifyDrive(createActionList(CLIENT_WORK_FOLDER), env.m_addrList);
        client.modifyDrive(createActionList_2(CLIENT_WORK_FOLDER), env.m_addrList);

        env.addDrive(DRIVE_PUB_KEY, 100 * 1024 * 1024);
        env.modifyDrive(DRIVE_PUB_KEY, {client.m_actionListHashes[0],
                                        client.m_modificationTransactionHashes[0],
                                        BIG_FILE_SIZE + 1024,
                                        env.m_addrList,
                                        client.m_clientKeyPair.publicKey()});

        env.modifyDrive(DRIVE_PUB_KEY, {client.m_actionListHashes[1],
                                        client.m_modificationTransactionHashes[1],
                                        BIG_FILE_SIZE + 1024,
                                        env.m_addrList,
                                        client.m_clientKeyPair.publicKey()});

        EXLOG("\n# Client asked to close drive");

        env.cancelModification(DRIVE_PUB_KEY, client.m_modificationTransactionHashes[0]);
        std::this_thread::sleep_for(std::chrono::seconds (60));
        ASSERT_EQ(env.modifyCompleteCounters[client.m_modificationTransactionHashes[0]], 0);
        ASSERT_EQ(env.modifyCompleteCounters[client.m_modificationTransactionHashes[1]], NUMBER_OF_REPLICATORS);
    }
}

#undef TEST_NAME

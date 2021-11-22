#include <drive/ExtensionEmulator.h>
#include "TestEnvironment.h"
#include "utils.h"

#include "types.h"
#include "drive/Session.h"
#include "drive/Utils.h"
#include "gtest/gtest.h"

using namespace sirius::drive::test;

namespace sirius::drive::test {

    /// change this macro for your test
#define TEST_NAME CloseDriveNoModification

#define ENVIRONMENT_CLASS JOIN(TEST_NAME, TestEnvironment)
#define RUN_TEST void JOIN(run, TEST_NAME)()

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
                startReplicator) {}
    };

    TEST(ModificationTest, TEST_NAME) {
        fs::remove_all(ROOT_FOLDER);

        lt::settings_pack pack;
        TestClient client(pack);

        _LOG("");

        ENVIRONMENT_CLASS env(
                NUMBER_OF_REPLICATORS, REPLICATOR_ADDRESS, PORT, DRIVE_ROOT_FOLDER,
                SANDBOX_ROOT_FOLDER, USE_TCP, 1, 1);

        EXLOG("\n# Client started: 1-st upload");
        auto actionList = createActionList(CLIENT_WORK_FOLDER);
        client.modifyDrive(actionList, env.m_addrList);

        env.addDrive(DRIVE_PUB_KEY, 100 * 1024 * 1024);

        EXLOG("\n# Client asked to close drive");

        env.closeDrive(DRIVE_PUB_KEY);
        std::thread([] {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            ASSERT_EQ(true, false);
        }).detach();
        env.waitDriveClosure();
    }

#undef TEST_NAME
}

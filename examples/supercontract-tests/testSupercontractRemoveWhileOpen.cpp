#include "TestEnvironment.h"
#include "drive/Utils.h"
#include "types.h"
#include "utils.h"
#include <fstream>
#include <numeric>

using namespace sirius::drive::test;

namespace sirius::drive::test {

/// change this macro for your test
#define TEST_NAME SupercontractRemoveWhileOpen

#define ENVIRONMENT_CLASS JOIN(TEST_NAME, TestEnvironment)

class ENVIRONMENT_CLASS
    : public TestEnvironment {
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
              startReplicator,
              true) {}
};

class TestHandlerRemoveWhileOpen {

public:
    std::promise<void> p;
    DriveKey m_driveKey;
    uint64_t m_fileId;
    uint64_t m_bytes;
    ENVIRONMENT_CLASS &m_env;

    TestHandlerRemoveWhileOpen(ENVIRONMENT_CLASS
                                   &env)
        : m_env(env) {}

public:
    void onFileRemoved(std::optional<RemoveResponse> res) {
        ASSERT_FALSE(res);
    }

    void onFileOpened(std::optional<OpenFileResponse> res) {
        ASSERT_TRUE(res);
        auto response = *res;
        ASSERT_TRUE(response.m_fileId);
        m_fileId = *response.m_fileId;
        m_bytes = 1024 * 1024;
        m_env.removeFsTreeEntry(m_driveKey, RemoveRequest{"test.txt", [this](auto res) {
                                                              onFileRemoved(res);
                                                          }});
    }

    void onSandboxModificationsInitiated(std::optional<InitiateSandboxModificationsResponse> res) {
        ASSERT_TRUE(res);
        m_env.openFile(m_driveKey, OpenFileRequest{OpenFileMode::READ, "test.txt", [this](auto res) { onFileOpened(res); }});
    }

    void onInitiatedModifications(std::optional<InitiateModificationsResponse> res) {
        ASSERT_TRUE(res);
        m_env.initiateManualSandboxModifications(m_driveKey, InitiateSandboxModificationsRequest{[this](auto res) {
                                                     onSandboxModificationsInitiated(
                                                         res);
                                                 }});
    }
};

TEST(SupercontractTest, TEST_NAME) {
    fs::remove_all(ROOT_FOLDER);
    EXLOG("");

    ENVIRONMENT_CLASS env(
        1, REPLICATOR_ADDRESS, PORT, DRIVE_ROOT_FOLDER,
        SANDBOX_ROOT_FOLDER, USE_TCP, 1, 1, 1024 * 1024);

    Key driveKey{{1}};
    env.addDrive(driveKey, Key(), 100 * 1024 * 1024);

    TestHandlerRemoveWhileOpen handler(env);
    handler.m_driveKey = driveKey;
    env.initiateManualModifications(driveKey,
                                    InitiateModificationsRequest{randomByteArray<Hash256>(), [&](auto res) { handler.onInitiatedModifications(res); }});

    handler.p.get_future().wait();
}

#undef TEST_NAME
} // namespace sirius::drive::test

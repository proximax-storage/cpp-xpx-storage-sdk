#include "TestEnvironment.h"
#include "drive/Utils.h"
#include "types.h"
#include "utils.h"
#include <fstream>
#include <numeric>

using namespace sirius::drive::test;

namespace sirius::drive::test {

/// change this macro for your test
#define TEST_NAME SupercontractIteratorNone

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

class IteratorNone {

public:
    std::promise<void> p;
    DriveKey m_driveKey;
    uint64_t m_fileId;
    ENVIRONMENT_CLASS &m_env;

    /*
    · drive
        test.txt
        test2.txt
    · unit
        test.txt
    · mod
        · gs
            test.txt
            test2.txt
        test.txt
        test2.txt
        test3.txt
    · sc
        test.txt
    · test
        test.txt
        test2.txt
    */
    const std::string EXPECTED[17] = {"drive", "test.txt", "test2.txt", "unit", "test.txt", "mod", "gs", "test.txt", "test2.txt", "test.txt", "test2.txt", "test3.txt", "sc", "test.txt", "test", "test.txt", "test2.txt"};
    int m_pointer = 0;

    IteratorNone(ENVIRONMENT_CLASS
                     &env)
        : m_env(env) {}

public:
    void onAppliedStorageModifications(std::optional<ApplyStorageModificationsResponse> res) {
        ASSERT_TRUE(res);
        p.set_value();
    }

    void onStorageHashEvaluated(std::optional<EvaluateStorageHashResponse> res) {
        ASSERT_TRUE(res);
        m_env.applyStorageManualModifications(m_driveKey, ApplyStorageModificationsRequest{true, [this](auto res) {
                                                                                               onAppliedStorageModifications(res);
                                                                                           }});
    }

    void onAppliedSandboxModifications(std::optional<ApplySandboxModificationsResponse> res) {
        ASSERT_TRUE(res);
        ASSERT_TRUE(res->m_success);
        m_env.evaluateStorageHash(m_driveKey, EvaluateStorageHashRequest{[this](auto res) {
                                      onStorageHashEvaluated(res);
                                  }});
    }

    void onIteratorDestryed(std::optional<FolderIteratorDestroyResponse> res) {
        ASSERT_TRUE(res);
        m_env.applySandboxManualModifications(m_driveKey, ApplySandboxModificationsRequest{true, [this](auto res) {
                                                                                               onAppliedSandboxModifications(res);
                                                                                           }});
    }

    void onNextRequested(std::optional<FolderIteratorNextResponse> res) {
        ASSERT_TRUE(res);
        std::string actual(res->m_name->begin(), res->m_name->end());
        // std::cout << actual << std::endl;
        ASSERT_EQ(EXPECTED[m_pointer], actual);
        m_pointer++;
        m_env.folderIteratorHasNext(m_driveKey, FolderIteratorHasNextRequest{m_fileId, [this](auto res) {
                                                                                 onNextConfirmed(res);
                                                                             }});
    }

    void onNextConfirmed(std::optional<FolderIteratorHasNextResponse> res) {
        ASSERT_TRUE(res);
        if (res->m_hasNext) {
            m_env.folderIteratorNext(m_driveKey, FolderIteratorNextRequest{m_fileId, [this](auto res) {
                                                                               onNextRequested(res);
                                                                           }});
        } else {
            m_env.folderIteratorDestroy(m_driveKey, FolderIteratorDestroyRequest{m_fileId, [this](auto res) {
                                                                                     onIteratorDestryed(res);
                                                                                 }});
        }
    }

    void onIteratorCreated(std::optional<FolderIteratorCreateResponse> res) {
        ASSERT_TRUE(res);
        auto response = *res;
        ASSERT_FALSE(response.m_id);
        m_env.folderIteratorDestroy(m_driveKey, FolderIteratorDestroyRequest{m_fileId, [this](auto res) {
                                                                                 onIteratorDestryed(res);
                                                                             }});
    }

    void onSandboxModificationsInitiated(std::optional<InitiateSandboxModificationsResponse> res) {
        ASSERT_TRUE(res);
        m_env.folderIteratorCreate(m_driveKey, FolderIteratorCreateRequest{"example", true, [this](auto res) { onIteratorCreated(res); }});
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

    IteratorNone handler(env);
    handler.m_driveKey = driveKey;
    env.initiateManualModifications(driveKey,
                                    InitiateModificationsRequest{randomByteArray<Hash256>(), [&](auto res) { handler.onInitiatedModifications(res); }});

    handler.p.get_future().wait();
}

#undef TEST_NAME
} // namespace sirius::drive::test

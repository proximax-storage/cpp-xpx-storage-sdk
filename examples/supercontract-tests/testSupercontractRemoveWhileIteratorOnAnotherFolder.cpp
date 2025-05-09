#include "TestEnvironment.h"
#include "drive/Utils.h"
#include "types.h"
#include "utils.h"
#include <fstream>
#include <numeric>

using namespace sirius::drive::test;

namespace sirius::drive::test {

/// change this macro for your test
#define TEST_NAME SupercontractRemoveWhileIteratorOnAnotherFolder

#define ENVIRONMENT_CLASS JOIN(TEST_NAME, TestEnvironment)

namespace {

class ENVIRONMENT_CLASS
    : public TestEnvironment {
public:
    ENVIRONMENT_CLASS(
        int numberOfReplicators,
        const std::string& ipAddr0,
        int port0,
        const std::string& rootFolder0,
        const std::string& sandboxRootFolder0,
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

class CreateFile {

public:
    std::promise<void> p;
    DriveKey m_driveKey;
    uint64_t m_fileId;
    ENVIRONMENT_CLASS& m_env;

    CreateFile(ENVIRONMENT_CLASS& env)
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

    void onFileClosed(std::optional<CloseFileResponse> res) {
        ASSERT_TRUE(res);
        ASSERT_TRUE(res->m_success);
        m_env.applySandboxManualModifications(m_driveKey, ApplySandboxModificationsRequest{true, [this](auto res) {
                                                                                               onAppliedSandboxModifications(res);
                                                                                           }});
    }

    void onFileFlushed(std::optional<FlushResponse> res) {
        ASSERT_TRUE(res);
        ASSERT_TRUE(res->m_success);
        m_env.closeFile(m_driveKey, CloseFileRequest{m_fileId, [this](auto res) {
                                                         onFileClosed(res);
                                                     }});
    }

    void onFileWritten(std::optional<WriteFileResponse> res) {
        ASSERT_TRUE(res);
        ASSERT_TRUE(res->m_success);
        m_env.flush(m_driveKey, FlushRequest{m_fileId, [this](auto res) {
                                                 onFileFlushed(res);
                                             }});
    }

    void onFileOpened(std::optional<OpenFileResponse> res) {
        ASSERT_TRUE(res);
        auto response = *res;
        ASSERT_TRUE(response.m_fileId);
        m_fileId = *response.m_fileId;
        std::string buffer = "data";
        m_env.writeFile(m_driveKey, WriteFileRequest{m_fileId, {buffer.begin(), buffer.end()}, [this](auto res) {
                                                         onFileWritten(res);
                                                     }});
    }

    void onDirCreated2(std::optional<CreateDirectoriesResponse> res) {
        ASSERT_TRUE(res);
        m_env.openFile(m_driveKey, OpenFileRequest{OpenFileMode::WRITE, "tests/test.txt", [this](auto res) { onFileOpened(res); }});
    }

    void onDirCreated(std::optional<CreateDirectoriesResponse> res) {
        ASSERT_TRUE(res);
        m_env.createDirectories(m_driveKey, CreateDirectoriesRequest{"move", [this](auto res) { onDirCreated2(res); }});
    }

    void onSandboxModificationsInitiated(std::optional<InitiateSandboxModificationsResponse> res) {
        ASSERT_TRUE(res);
        m_env.createDirectories(m_driveKey, CreateDirectoriesRequest{"tests", [this](auto res) { onDirCreated(res); }});
    }

    void onInitiatedModifications(std::optional<InitiateModificationsResponse> res) {
        ASSERT_TRUE(res);
        m_env.initiateManualSandboxModifications(m_driveKey, InitiateSandboxModificationsRequest{[this](auto res) {
                                                     onSandboxModificationsInitiated(
                                                         res);
                                                 }});
    }
};

class RemoveWhileIteratingOnAnotherFolder {

public:
    std::promise<void> p;
    DriveKey m_driveKey;
    uint64_t m_fileId;
    ENVIRONMENT_CLASS& m_env;

    RemoveWhileIteratingOnAnotherFolder(ENVIRONMENT_CLASS& env)
        : m_env(env) {}

public:
    void onFileRemoved(std::optional<RemoveFilesystemEntryResponse> res) {
        ASSERT_TRUE(res);
        ASSERT_TRUE(res->m_success);
        p.set_value();
    }

    void onIterCreated(std::optional<FolderIteratorCreateResponse> res) {
        ASSERT_TRUE(res);
        m_env.removeFsTreeEntry(m_driveKey, RemoveFilesystemEntryRequest{"tests/test.txt", [this](auto res) { onFileRemoved(res); }});
    }

    void onSandboxModificationsInitiated(std::optional<InitiateSandboxModificationsResponse> res) {
        ASSERT_TRUE(res);
        m_env.folderIteratorCreate(m_driveKey, FolderIteratorCreateRequest{"move", true, [this](auto res) { onIterCreated(res); }});
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

    CreateFile handler(env);
    handler.m_driveKey = driveKey;
    env.initiateManualModifications(driveKey,
                                    InitiateModificationsRequest{randomByteArray<Hash256>(), [&](auto res) { handler.onInitiatedModifications(res); }});

    handler.p.get_future().wait();

    RemoveWhileIteratingOnAnotherFolder handler_r(env);
    handler_r.m_driveKey = driveKey;
    env.initiateManualModifications(driveKey,
                                    InitiateModificationsRequest{randomByteArray<Hash256>(), [&](auto res) { handler_r.onInitiatedModifications(res); }});

    handler_r.p.get_future().wait();
}
} // namespace

#undef TEST_NAME
} // namespace sirius::drive::test

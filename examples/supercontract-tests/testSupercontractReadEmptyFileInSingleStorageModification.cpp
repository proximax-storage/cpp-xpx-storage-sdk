#include "TestEnvironment.h"
#include "drive/Utils.h"
#include "types.h"
#include "utils.h"
#include <fstream>
#include <numeric>

using namespace sirius::drive::test;

namespace sirius::drive::test {

/// change this macro for your test
#define TEST_NAME SupercontractReadEmptyFileInSingleStorageModification

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

class TestHandler {

public:
    std::promise<void> p;
    DriveKey m_driveKey;
    uint64_t m_fileId;
    uint64_t m_bytes;
    ENVIRONMENT_CLASS& m_env;

    TestHandler(ENVIRONMENT_CLASS& env)
        : m_env(env) {}

public:
    void onReceivedAbsolutePath(std::optional<AbsolutePathResponse> res) {
        ASSERT_TRUE(res);
        std::ostringstream stream;
        const auto& path = res->m_path;
        ASSERT_TRUE(fs::exists(path));
        std::ifstream fileStream(path);
        stream << fileStream.rdbuf();
        auto content = stream.str();
        ASSERT_EQ(content, "");
        p.set_value();
    }

    void onReceivedFsTree(std::optional<FilesystemResponse> res) {
        ASSERT_TRUE(res);
        auto& fsTree = res->m_fsTree;
        ASSERT_TRUE(fsTree.childs().size() == 1);
        const auto& child = fsTree.childs().begin()->second;
        ASSERT_TRUE(isFile(child));
        const auto& file = getFile(child);
        ASSERT_TRUE(file.name() == "test.txt");
        m_env.getAbsolutePath(m_driveKey, AbsolutePathRequest{"test.txt", [this](auto res) {
                                                                  onReceivedAbsolutePath(res);
                                                              }});
    }

    void onAppliedStorageModifications(std::optional<ApplyStorageModificationsResponse> res) {
        ASSERT_TRUE(res);
        m_env.getFilesystem(m_driveKey, FilesystemRequest{[this](auto res) {
                                onReceivedFsTree(res);
                            }});
    }

    void onStorageHashEvaluated(std::optional<EvaluateStorageHashResponse> res) {
        ASSERT_TRUE(res);
        m_env.applyStorageManualModifications(m_driveKey, ApplyStorageModificationsRequest{true, [this](auto res) {
                                                                                               onAppliedStorageModifications(res);
                                                                                           }});
    }

    void onAppliedSandboxModifications2(std::optional<ApplySandboxModificationsResponse> res) {
        ASSERT_TRUE(res);
        ASSERT_TRUE(res->m_success);
        m_env.evaluateStorageHash(m_driveKey, EvaluateStorageHashRequest{[this](auto res) {
                                      onStorageHashEvaluated(res);
                                  }});
    }

    void onFileClosed2(std::optional<CloseFileResponse> res) {
        ASSERT_TRUE(res);
        ASSERT_TRUE(res->m_success);
        m_env.applySandboxManualModifications(m_driveKey, ApplySandboxModificationsRequest{true, [this](auto res) {
                                                                                               onAppliedSandboxModifications2(res);
                                                                                           }});
    }

    void onFileRead2(std::optional<ReadFileResponse> res) {
        ASSERT_TRUE(res);
        auto buffer = res->m_buffer;
        std::string actual(buffer->begin(), buffer->end());
        ASSERT_EQ(actual, "");
        m_env.closeFile(m_driveKey, CloseFileRequest{m_fileId, [this](auto res) {
                                                         onFileClosed2(res);
                                                     }});
    }

    void onFileOpened2(std::optional<OpenFileResponse> res) {
        ASSERT_TRUE(res);
        auto response = *res;
        ASSERT_TRUE(response.m_fileId);
        m_fileId = *response.m_fileId;
        m_bytes = 1024 * 1024;
        m_env.readFile(m_driveKey, ReadFileRequest{m_fileId, m_bytes, [this](auto res) {
                                                       onFileRead2(res);
                                                   }});
    }

    void onSandboxModificationsInitiated2(std::optional<InitiateSandboxModificationsResponse> res) {
        ASSERT_TRUE(res);
        m_env.openFile(m_driveKey, OpenFileRequest{OpenFileMode::READ, "test.txt", [this](auto res) { onFileOpened2(res); }});
    }

    void onAppliedSandboxModifications(std::optional<ApplySandboxModificationsResponse> res) {
        ASSERT_TRUE(res);
        ASSERT_TRUE(res->m_success);
        m_env.initiateManualSandboxModifications(m_driveKey, InitiateSandboxModificationsRequest{[this](auto res) {
                                                     onSandboxModificationsInitiated2(
                                                         res);
                                                 }});
    }

    void onFileClosed(std::optional<CloseFileResponse> res) {
        ASSERT_TRUE(res);
        ASSERT_TRUE(res->m_success);
        m_env.applySandboxManualModifications(m_driveKey, ApplySandboxModificationsRequest{true, [this](auto res) {
                                                                                               onAppliedSandboxModifications(res);
                                                                                           }});
    }

    void onFileOpened(std::optional<OpenFileResponse> res) {
        ASSERT_TRUE(res);
        m_fileId = *res->m_fileId;
        m_env.closeFile(m_driveKey, CloseFileRequest{m_fileId, [this](auto res) {
                                                         onFileClosed(res);
                                                     }});
    }

    void onSandboxModificationsInitiated(std::optional<InitiateSandboxModificationsResponse> res) {
        ASSERT_TRUE(res);
        m_env.openFile(m_driveKey, OpenFileRequest{OpenFileMode::WRITE, "test.txt", [this](auto res) { onFileOpened(res); }});
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
    std::chrono::milliseconds span(2000);

    TestHandler handlerw(env);
    handlerw.m_driveKey = driveKey;
    env.initiateManualModifications(driveKey,
                                    InitiateModificationsRequest{randomByteArray<Hash256>(), [&](auto res) { handlerw.onInitiatedModifications(res); }});

    handlerw.p.get_future().wait_for(span);
}
} // namespace

#undef TEST_NAME
} // namespace sirius::drive::test

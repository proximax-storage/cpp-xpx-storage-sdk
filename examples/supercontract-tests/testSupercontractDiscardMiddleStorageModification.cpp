#include "TestEnvironment.h"
#include "drive/Utils.h"
#include "types.h"
#include "utils.h"
#include <fstream>
#include <numeric>

using namespace sirius::drive::test;

namespace sirius::drive::test {

/// change this macro for your test
#define TEST_NAME SupercontractDiscardMiddleStorageModification

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

class DiscardMiddleStorage {

public:
    std::promise<void> p;
    DriveKey m_driveKey;
    uint64_t m_fileId;
    int m_counter = 0;
    ENVIRONMENT_CLASS& m_env;

    DiscardMiddleStorage(ENVIRONMENT_CLASS& env)
        : m_env(env) {}

public:
    void onReceivedFsTree(std::optional<FilesystemResponse> res) {
        ASSERT_TRUE(res);
        auto& fsTree = res->m_fsTree;
        ASSERT_EQ(fsTree.childs().size(), 2);
        std::vector<std::string> actual;
        for (const auto& [key, val] : fsTree.childs()) {
            ASSERT_TRUE(isFile(val));
            auto file = getFile(val);
            actual.push_back(file.name());
        }
        std::vector<std::string> expected = {"test.txt", "test3.txt"};
        ASSERT_EQ(actual, expected);
        p.set_value();
    }

    void onAppliedStorageModifications3(std::optional<ApplyStorageModificationsResponse> res) {
        ASSERT_TRUE(res);
        m_env.getFilesystem(m_driveKey, FilesystemRequest{[this](auto res) {
                                onReceivedFsTree(res);
                            }});
    }

    void onStorageHashEvaluated3(std::optional<EvaluateStorageHashResponse> res) {
        ASSERT_TRUE(res);
        m_env.applyStorageManualModifications(m_driveKey, ApplyStorageModificationsRequest{true, [this](auto res) {
                                                                                               onAppliedStorageModifications3(res);
                                                                                           }});
    }

    void onAppliedSandboxModifications3(std::optional<ApplySandboxModificationsResponse> res) {
        ASSERT_TRUE(res);
        ASSERT_TRUE(res->m_success);
        m_env.evaluateStorageHash(m_driveKey, EvaluateStorageHashRequest{[this](auto res) {
                                      onStorageHashEvaluated3(res);
                                  }});
    }

    void onFileClosed3(std::optional<CloseFileResponse> res) {
        ASSERT_TRUE(res);
        ASSERT_TRUE(res->m_success);
        m_env.applySandboxManualModifications(m_driveKey, ApplySandboxModificationsRequest{true, [this](auto res) {
                                                                                               onAppliedSandboxModifications3(res);
                                                                                           }});
    }

    void onFileFlushed3(std::optional<FlushResponse> res) {
        ASSERT_TRUE(res);
        ASSERT_TRUE(res->m_success);
        m_env.closeFile(m_driveKey, CloseFileRequest{m_fileId, [this](auto res) {
                                                         onFileClosed3(res);
                                                     }});
    }

    void onFileWritten3(std::optional<WriteFileResponse> res) {
        ASSERT_TRUE(res);
        ASSERT_TRUE(res->m_success);
        m_env.flush(m_driveKey, FlushRequest{m_fileId, [this](auto res) {
                                                 onFileFlushed3(res);
                                             }});
    }

    void onFileOpened3(std::optional<OpenFileResponse> res) {
        ASSERT_TRUE(res);
        auto response = *res;
        ASSERT_TRUE(response.m_fileId);
        m_fileId = *response.m_fileId;
        std::string buffer = "data data data";
        m_env.writeFile(m_driveKey, WriteFileRequest{m_fileId, {buffer.begin(), buffer.end()}, [this](auto res) {
                                                         onFileWritten3(res);
                                                     }});
    }

    void onSandboxModificationsInitiated3(std::optional<InitiateSandboxModificationsResponse> res) {
        ASSERT_TRUE(res);
        m_env.openFile(m_driveKey, OpenFileRequest{OpenFileMode::WRITE, "test3.txt", [this](auto res) { onFileOpened3(res); }});
    }

    void onInitiatedModifications3(std::optional<InitiateModificationsResponse> res) {
        ASSERT_TRUE(res);
        m_env.initiateManualSandboxModifications(m_driveKey, InitiateSandboxModificationsRequest{[this](auto res) {
                                                     onSandboxModificationsInitiated3(
                                                         res);
                                                 }});
    }

    void onAppliedStorageModifications2(std::optional<ApplyStorageModificationsResponse> res) {
        ASSERT_TRUE(res);
        m_env.initiateManualModifications(m_driveKey,
                                          InitiateModificationsRequest{randomByteArray<Hash256>(), [&](auto res) { onInitiatedModifications3(res); }});
    }

    void onStorageHashEvaluated2(std::optional<EvaluateStorageHashResponse> res) {
        ASSERT_TRUE(res);
        m_env.applyStorageManualModifications(m_driveKey, ApplyStorageModificationsRequest{false, [this](auto res) {
                                                                                               onAppliedStorageModifications2(res);
                                                                                           }});
    }

    void onAppliedSandboxModifications2(std::optional<ApplySandboxModificationsResponse> res) {
        ASSERT_TRUE(res);
        ASSERT_TRUE(res->m_success);
        m_env.evaluateStorageHash(m_driveKey, EvaluateStorageHashRequest{[this](auto res) {
                                      onStorageHashEvaluated2(res);
                                  }});
    }

    void onFileClosed2(std::optional<CloseFileResponse> res) {
        ASSERT_TRUE(res);
        ASSERT_TRUE(res->m_success);
        m_env.applySandboxManualModifications(m_driveKey, ApplySandboxModificationsRequest{true, [this](auto res) {
                                                                                               onAppliedSandboxModifications2(res);
                                                                                           }});
    }

    void onFileFlushed2(std::optional<FlushResponse> res) {
        ASSERT_TRUE(res);
        ASSERT_TRUE(res->m_success);
        m_env.closeFile(m_driveKey, CloseFileRequest{m_fileId, [this](auto res) {
                                                         onFileClosed2(res);
                                                     }});
    }

    void onFileWritten2(std::optional<WriteFileResponse> res) {
        ASSERT_TRUE(res);
        ASSERT_TRUE(res->m_success);
        m_env.flush(m_driveKey, FlushRequest{m_fileId, [this](auto res) {
                                                 onFileFlushed2(res);
                                             }});
    }

    void onFileOpened2(std::optional<OpenFileResponse> res) {
        ASSERT_TRUE(res);
        auto response = *res;
        ASSERT_TRUE(response.m_fileId);
        m_fileId = *response.m_fileId;
        std::string buffer = "data data";
        m_env.writeFile(m_driveKey, WriteFileRequest{m_fileId, {buffer.begin(), buffer.end()}, [this](auto res) {
                                                         onFileWritten2(res);
                                                     }});
    }

    void onSandboxModificationsInitiated2(std::optional<InitiateSandboxModificationsResponse> res) {
        ASSERT_TRUE(res);
        m_env.openFile(m_driveKey, OpenFileRequest{OpenFileMode::WRITE, "test2.txt", [this](auto res) { onFileOpened2(res); }});
    }

    void onInitiatedModifications2(std::optional<InitiateModificationsResponse> res) {
        ASSERT_TRUE(res);
        m_env.initiateManualSandboxModifications(m_driveKey, InitiateSandboxModificationsRequest{[this](auto res) {
                                                     onSandboxModificationsInitiated2(
                                                         res);
                                                 }});
    }

    void onAppliedStorageModifications(std::optional<ApplyStorageModificationsResponse> res) {
        ASSERT_TRUE(res);
        m_env.initiateManualModifications(m_driveKey,
                                          InitiateModificationsRequest{randomByteArray<Hash256>(), [&](auto res) { onInitiatedModifications2(res); }});
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

    DiscardMiddleStorage handler(env);
    handler.m_driveKey = driveKey;
    env.initiateManualModifications(driveKey,
                                    InitiateModificationsRequest{randomByteArray<Hash256>(), [&](auto res) { handler.onInitiatedModifications(res); }});

    handler.p.get_future().wait();
}
} // namespace

#undef TEST_NAME
} // namespace sirius::drive::test

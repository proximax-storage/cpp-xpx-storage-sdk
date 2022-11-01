#include "TestEnvironment.h"
#include "drive/Utils.h"
#include "types.h"
#include "utils.h"
#include <fstream>
#include <numeric>

using namespace sirius::drive::test;

namespace sirius::drive::test {

/// change this macro for your test
#define TEST_NAME SupercontractMoveAndWriteBeforeModification

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

class TestHandlerCreate {

public:
    std::promise<void> p;
    DriveKey m_driveKey;
    uint64_t m_fileId;
    ENVIRONMENT_CLASS& m_env;

    TestHandlerCreate(ENVIRONMENT_CLASS& env)
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
        ASSERT_EQ(content, "data");
        p.set_value();
    }

    void onReceivedFsTree(std::optional<FilesystemResponse> res) {
        ASSERT_TRUE(res);
        auto& fsTree = res->m_fsTree;
        ASSERT_TRUE(fsTree.childs().size() == 1);
        const auto& child = fsTree.childs().begin()->second;
        ASSERT_TRUE(isFolder(child));
        const auto& folder = getFolder(child);
        ASSERT_TRUE(folder.name() == "tests");
        const auto& files = folder.childs();
        for (auto const& [key, val] : files) {
            ASSERT_TRUE(isFile(val));
            const auto& file = getFile(val);
            ASSERT_TRUE(file.name() == "test.txt");
        }
        m_env.getAbsolutePath(m_driveKey, AbsolutePathRequest{"tests/test.txt", [this](auto res) {
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

    void onDirCreated(std::optional<CreateDirectoriesResponse> res) {
        ASSERT_TRUE(res);
        m_env.openFile(m_driveKey, OpenFileRequest{OpenFileMode::WRITE, "tests/test.txt", [this](auto res) { onFileOpened(res); }});
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

class TestHandlerWriteMovedFile {
public:
    std::promise<void> p;
    DriveKey m_driveKey;
    uint64_t m_fileId;
    uint64_t m_bytes;
    ENVIRONMENT_CLASS& m_env;

    TestHandlerWriteMovedFile(ENVIRONMENT_CLASS& env)
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
        std::string buffer = "data data";
        m_env.writeFile(m_driveKey, WriteFileRequest{m_fileId, {buffer.begin(), buffer.end()}, [this](auto res) {
                                                         onFileWritten(res);
                                                     }});
    }

    void onFileMoved(std::optional<MoveFilesystemEntryResponse> res) {
        ASSERT_TRUE(res);
        ASSERT_TRUE(res->m_success);
        m_env.openFile(m_driveKey, OpenFileRequest{OpenFileMode::WRITE, "tests/test.txt", [this](auto res) { onFileOpened(res); }});
    }

    void onDirCreated(std::optional<CreateDirectoriesResponse> res) {
        ASSERT_TRUE(res);
        m_env.moveFsTreeEntry(m_driveKey, MoveFilesystemEntryRequest{"tests/test.txt", "moved/test.txt", [this](auto res) { onFileMoved(res); }});
    }

    void onSandboxModificationsInitiated(std::optional<InitiateSandboxModificationsResponse> res) {
        ASSERT_TRUE(res);
        m_env.createDirectories(m_driveKey, CreateDirectoriesRequest{"moved", [this](auto res) { onDirCreated(res); }});
    }

    void onInitiatedModifications(std::optional<InitiateModificationsResponse> res) {
        ASSERT_TRUE(res);
        m_env.initiateManualSandboxModifications(m_driveKey, InitiateSandboxModificationsRequest{[this](auto res) {
                                                     onSandboxModificationsInitiated(
                                                         res);
                                                 }});
    }
};

class TestHandlerReadMovedFileAssert {

public:
    std::promise<void> p;
    DriveKey m_driveKey;
    uint64_t m_fileId;
    uint64_t m_bytes;
    std::string m_filename;
    std::string m_expected;
    ENVIRONMENT_CLASS& m_env;

    TestHandlerReadMovedFileAssert(ENVIRONMENT_CLASS& env, std::string filename, std::string expected)
        : m_env(env), m_filename(filename), m_expected(expected) {}

public:
    void onReceivedAbsolutePath(std::optional<AbsolutePathResponse> res) {
        ASSERT_TRUE(res);
        std::ostringstream stream;
        const auto& path = res->m_path;
        ASSERT_TRUE(fs::exists(path));
        std::ifstream fileStream(path);
        stream << fileStream.rdbuf();
        auto content = stream.str();
        ASSERT_EQ(content, m_expected);
        p.set_value();
    }

    void onReceivedFsTree(std::optional<FilesystemResponse> res) {
        ASSERT_TRUE(res);
        auto& fsTree = res->m_fsTree;
        ASSERT_TRUE(fsTree.childs().size() == 2);
        for (auto const& [key, val] : fsTree.childs()) {
            const auto& child = fsTree.childs().begin()->second;
            ASSERT_TRUE(isFolder(child));
            const auto& folder = getFolder(child);
            // if (folder.name() == "tests") {
            //     ASSERT_TRUE(folder.childs().size() == 1);
            // }
            // ASSERT_TRUE(folder.name() == "moved");
            const auto& files = folder.childs();
            for (auto const& [key, val] : files) {
                ASSERT_TRUE(isFile(val));
                const auto& file = getFile(val);
                ASSERT_TRUE(file.name() == "test.txt");
            }
        }
        m_env.getAbsolutePath(m_driveKey, AbsolutePathRequest{m_filename, [this](auto res) {
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

    void onFileRead(std::optional<ReadFileResponse> res) {
        ASSERT_TRUE(res);
        auto buffer = res->m_buffer;
        std::string actual(buffer->begin(), buffer->end());
        ASSERT_EQ(actual, m_expected);
        m_env.closeFile(m_driveKey, CloseFileRequest{m_fileId, [this](auto res) {
                                                         onFileClosed(res);
                                                     }});
    }

    void onFileOpened(std::optional<OpenFileResponse> res) {
        ASSERT_TRUE(res);
        auto response = *res;
        ASSERT_TRUE(response.m_fileId);
        m_fileId = *response.m_fileId;
        m_bytes = 1024 * 1024;
        m_env.readFile(m_driveKey, ReadFileRequest{m_fileId, m_bytes, [this](auto res) {
                                                       onFileRead(res);
                                                   }});
    }

    void onSandboxModificationsInitiated(std::optional<InitiateSandboxModificationsResponse> res) {
        ASSERT_TRUE(res);
        m_env.openFile(m_driveKey, OpenFileRequest{OpenFileMode::READ, m_filename, [this](auto res) { onFileOpened(res); }});
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

    TestHandlerCreate handler(env);
    handler.m_driveKey = driveKey;
    env.initiateManualModifications(driveKey,
                                    InitiateModificationsRequest{randomByteArray<Hash256>(), [&](auto res) { handler.onInitiatedModifications(res); }});

    handler.p.get_future().wait();

    TestHandlerWriteMovedFile handlerm(env);
    handlerm.m_driveKey = driveKey;
    env.initiateManualModifications(driveKey,
                                    InitiateModificationsRequest{randomByteArray<Hash256>(), [&](auto res) { handlerm.onInitiatedModifications(res); }});

    handlerm.p.get_future().wait();

    TestHandlerReadMovedFileAssert handlerr1(env, "moved/test.txt", "data");
    handlerr1.m_driveKey = driveKey;
    env.initiateManualModifications(driveKey,
                                    InitiateModificationsRequest{randomByteArray<Hash256>(), [&](auto res) { handlerr1.onInitiatedModifications(res); }});

    handlerr1.p.get_future().wait();

    TestHandlerReadMovedFileAssert handlerr2(env, "tests/test.txt", "data data");
    handlerr2.m_driveKey = driveKey;
    env.initiateManualModifications(driveKey,
                                    InitiateModificationsRequest{randomByteArray<Hash256>(), [&](auto res) { handlerr2.onInitiatedModifications(res); }});

    handlerr2.p.get_future().wait();
}
} // namespace

#undef TEST_NAME
} // namespace sirius::drive::test

#include "TestEnvironment.h"
#include "drive/Utils.h"
#include "types.h"
#include "utils.h"
#include <fstream>
#include <numeric>

using namespace sirius::drive::test;

namespace sirius::drive::test {

/// change this macro for your test
#define TEST_NAME SupercontractRemoveFileInSingleStorageModification

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
        public:
            void onReceivedFsTree(std::optional<FilesystemResponse> res) {
                ASSERT_TRUE(res);
                auto& fsTree = res->m_fsTree;
                ASSERT_TRUE(fsTree.childs().size() == 0);
                p.set_value();
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

            void onFileRemoved(std::optional<RemoveFilesystemEntryResponse> res) {
                ASSERT_TRUE(res);
                ASSERT_TRUE(res->m_success);
                m_env.applySandboxManualModifications(m_driveKey, ApplySandboxModificationsRequest{true, [this](auto res) {
                    onAppliedSandboxModifications2(res);
                }});
            }


            void onSandboxModificationsInitiated2(std::optional<InitiateSandboxModificationsResponse> res) {
                ASSERT_TRUE(res);
                m_env.removeFsTreeEntry(m_driveKey, RemoveFilesystemEntryRequest{"test.txt", [this](auto res) {
                    onFileRemoved(res);
                }});            }

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

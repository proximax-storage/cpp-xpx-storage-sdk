#include "TestEnvironment.h"
#include "drive/Utils.h"
#include "types.h"
#include "utils.h"
#include <fstream>
#include <numeric>

using namespace sirius::drive::test;

namespace sirius::drive::test {

/// Change this macro for your test
#define TEST_NAME SupercontractOpenAlreadyOpenedFile

#define ENVIRONMENT_CLASS JOIN(TEST_NAME, TestEnvironment)

namespace {

    class ENVIRONMENT_CLASS : public TestEnvironment {
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

    class TestHandlerOpenAlreadyOpenedFile {
    public:
        TestHandlerOpenAlreadyOpenedFile(
                ENVIRONMENT_CLASS& environment,
                DriveKey driveKey)
                : m_environment(environment)
                , m_driveKey(std::move(driveKey)) {}

    public:
        std::promise<void>& promise() {
            return m_promise;
        };

        void initiateSandboxModifications(
                const std::string& fileAbsolutePath,
                OpenFileMode openFileMode) {
            m_fileAbsolutePath = fileAbsolutePath;
            m_openFileMode = openFileMode;
            m_environment.initiateManualSandboxModifications(
                    m_driveKey,
                    InitiateSandboxModificationsRequest{
                        [&](auto response) { onSandboxModificationsInitiated(response); }
                    });
        }

    private:
        void onStorageModificationsApplied(std::optional<ApplyStorageModificationsResponse> response) {
            ASSERT_TRUE(response);
            m_promise.set_value();
        }

        void onSandboxModificationsApplied(std::optional<ApplySandboxModificationsResponse> response) {
            ASSERT_TRUE(response);
            ASSERT_TRUE(response->m_success);
            m_environment.applyStorageManualModifications(
                    m_driveKey,
                    ApplyStorageModificationsRequest{
                        true,
                        [this](auto response) { onStorageModificationsApplied(response); }
                    });
        }

        void onFileClosedAfterSecondOpening(std::optional<CloseFileResponse> response) {
            ASSERT_TRUE(response);
            ASSERT_TRUE(response->m_success);
            m_environment.applySandboxManualModifications(
                    m_driveKey,
                    ApplySandboxModificationsRequest{
                        true,
                        [this](auto response) { onSandboxModificationsApplied(response); }
                    });
        }

        void onFileOpenedAfterClosing(std::optional<OpenFileResponse> response) {
            ASSERT_TRUE(response);
            auto responseValue = *response;
            ASSERT_TRUE(responseValue.m_fileId);
            m_fileId = *responseValue.m_fileId;

            m_environment.closeFile(
                    m_driveKey,
                    CloseFileRequest{
                        m_fileId,
                        [this](auto response) { onFileClosedAfterSecondOpening(response); }
                    });
        }

        void onFileClosed(std::optional<CloseFileResponse> response) {
            ASSERT_TRUE(response);
            ASSERT_TRUE(response->m_success);

            // Attempt to open the file after closing:
            m_environment.openFile(
                    m_driveKey,
                    OpenFileRequest{
                        m_openFileMode,
                        m_fileAbsolutePath,
                        [this](auto response) { onFileOpenedAfterClosing(response); }
                    });
        }

        void onFileRepeatedOpeningAttempted(std::optional<OpenFileResponse> response) {
            ASSERT_TRUE(response);

            // On attempt to open an already opened file, response should not return a file ID:
            ASSERT_FALSE(response->m_fileId);

            m_environment.closeFile(
                    m_driveKey,
                    CloseFileRequest{
                        m_fileId,
                        [this](auto response) { onFileClosed(response); }
                    });
        }

        void onFileOpened(std::optional<OpenFileResponse> response) {
            ASSERT_TRUE(response);
            auto responseValue = *response;
            ASSERT_TRUE(responseValue.m_fileId);
            m_fileId = *responseValue.m_fileId;

            // Attempt to open the same file again:
            m_environment.openFile(
                    m_driveKey,
                    OpenFileRequest{
                        m_openFileMode,
                        m_fileAbsolutePath,
                        [this](auto response) { onFileRepeatedOpeningAttempted(response); }
                    });
        }

        void onSandboxModificationsInitiated(std::optional<InitiateSandboxModificationsResponse> response) {
            ASSERT_TRUE(response);
            m_environment.openFile(
                    m_driveKey,
                    OpenFileRequest{
                        m_openFileMode,
                        m_fileAbsolutePath,
                        [this](auto response) { onFileOpened(response); }
                    });
        }

    private:
        std::promise<void> m_promise;
        ENVIRONMENT_CLASS& m_environment;
        DriveKey m_driveKey;
        std::string m_fileAbsolutePath;
        uint64_t m_fileId;
        OpenFileMode m_openFileMode;
    };

    TEST(SupercontractTest, TEST_NAME) {
        fs::remove_all(ROOT_FOLDER);
        EXLOG("");

        ENVIRONMENT_CLASS environment(
                1, REPLICATOR_ADDRESS, PORT, DRIVE_ROOT_FOLDER,
                SANDBOX_ROOT_FOLDER, USE_TCP, 1, 1, 1024 * 1024);

        Key driveKey{{1}};
        environment.addDrive(driveKey, Key(), 100 * 1024 * 1024);

        TestHandlerPrepareFsTree<ENVIRONMENT_CLASS> preparationHandler(environment, driveKey);
        preparationHandler.initiateManualModifications({
            {"test.txt", 1u}
        });
        preparationHandler.promise().get_future().wait();

        TestHandlerOpenAlreadyOpenedFile mainHandler(environment, driveKey);
        mainHandler.initiateSandboxModifications(
                "test.txt",
                OpenFileMode::READ);
        mainHandler.promise().get_future().wait();
    }
} // namespace

#undef TEST_NAME
} // namespace sirius::drive::test

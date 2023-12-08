#include "TestEnvironment.h"
#include "drive/Utils.h"
#include "types.h"
#include "utils.h"
#include <boost/algorithm/string.hpp>
#include <fstream>
#include <numeric>

using namespace sirius::drive::test;

namespace sirius::drive::test {

/// Change this macro for your test
#define TEST_NAME SupercontractCreateIteratorWithOpenedFile

#define ENVIRONMENT_CLASS JOIN(TEST_NAME, TestEnvironment)
#define VARIANT_TEST_NAME(SUFFIX) JOIN(TEST_NAME, SUFFIX)

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

    class TestHandlerCreateIteratorWithOpenedFile {
    public:
        TestHandlerCreateIteratorWithOpenedFile(
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
                const std::vector<std::pair<std::string, bool>>& testedDirectories,
                OpenFileMode openFileMode,
                bool recursiveIterators) {
            m_fileAbsolutePath = fileAbsolutePath;
            m_testedDirectories = testedDirectories;
            m_directoryIter = m_testedDirectories.begin();
            m_openFileMode = openFileMode;
            m_recursiveIterators = recursiveIterators;
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

        void onFileClosed(std::optional<CloseFileResponse> response) {
            ASSERT_TRUE(response);
            ASSERT_TRUE(response->m_success);
            m_environment.applySandboxManualModifications(
                    m_driveKey,
                    ApplySandboxModificationsRequest{
                        true,
                        [this](auto response) { onSandboxModificationsApplied(response); }
                    });
        }

        void onIteratorCreationAttempted(std::optional<FolderIteratorCreateResponse> response) {
            ASSERT_TRUE(response);

            // On attempt to create an iterator for a directory that contains (directly or indirectly) an opened file,
            // response should not return a valid iterator ID:
            const bool expectedResult = m_directoryIter->second;
            ASSERT_EQ(static_cast<bool>(response->m_id), expectedResult);

            ++m_directoryIter;
            if (m_directoryIter != m_testedDirectories.end()) {
                const auto& directoryAbsolutePath = m_directoryIter->first;
                m_environment.folderIteratorCreate(
                        m_driveKey,
                        FolderIteratorCreateRequest{
                            directoryAbsolutePath,
                            m_recursiveIterators,
                            [this](auto response) { onIteratorCreationAttempted(response); }
                        });
            } else {
                m_environment.closeFile(
                        m_driveKey,
                        CloseFileRequest{
                            m_fileId,
                            [this](auto response) { onFileClosed(response); }
                        });
            }
        }

        void onFileOpened(std::optional<OpenFileResponse> response) {
            ASSERT_TRUE(response);
            auto responseValue = *response;
            ASSERT_TRUE(responseValue.m_fileId);
            m_fileId = *responseValue.m_fileId;

            // Attempt to create a folder iterator:
            const auto& directoryAbsolutePath = m_directoryIter->first;
            m_environment.folderIteratorCreate(
                    m_driveKey,
                    FolderIteratorCreateRequest{
                        directoryAbsolutePath,
                        m_recursiveIterators,
                        [this](auto response) { onIteratorCreationAttempted(response); }
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
        std::vector<std::pair<std::string, bool>> m_testedDirectories;
        std::vector<std::pair<std::string, bool>>::iterator m_directoryIter;
        uint64_t m_fileId;
        OpenFileMode m_openFileMode;
        bool m_recursiveIterators;
    };

    TEST(SupercontractTest, VARIANT_TEST_NAME(Recursive)) {
        fs::remove_all(ROOT_FOLDER);
        EXLOG("");

        ENVIRONMENT_CLASS environment(
                1, REPLICATOR_ADDRESS, PORT, DRIVE_ROOT_FOLDER,
                SANDBOX_ROOT_FOLDER, USE_TCP, 1, 1, 1024 * 1024);

        Key driveKey{{1}};
        environment.addDrive(driveKey, Key(), 100 * 1024 * 1024);

        TestHandlerPrepareFsTree<ENVIRONMENT_CLASS> preparationHandler(environment, driveKey);
        preparationHandler.initiateManualModifications({
            {"outer/inner", {
                {"test.txt", 1u}}},
            {"outer/sibling", {}},
        });
        preparationHandler.promise().get_future().wait();

        TestHandlerCreateIteratorWithOpenedFile mainHandler(environment, driveKey);
        mainHandler.initiateSandboxModifications(
                "outer/inner/test.txt",
                {
                        {"outer/inner", false},
                        {"outer", false},
                        {"outer/sibling", true}},
                OpenFileMode::READ,
                true);
        mainHandler.promise().get_future().wait();
    }

    TEST(SupercontractTest, VARIANT_TEST_NAME(NonRecursive)) {
        fs::remove_all(ROOT_FOLDER);
        EXLOG("");

        ENVIRONMENT_CLASS environment(
                1, REPLICATOR_ADDRESS, PORT, DRIVE_ROOT_FOLDER,
                SANDBOX_ROOT_FOLDER, USE_TCP, 1, 1, 1024 * 1024);

        Key driveKey{{1}};
        environment.addDrive(driveKey, Key(), 100 * 1024 * 1024);

        TestHandlerPrepareFsTree<ENVIRONMENT_CLASS> preparationHandler(environment, driveKey);
        preparationHandler.initiateManualModifications({
            {"outer/inner", {
                {"test.txt", 1u}}},
            {"outer/sibling", {}},
        });
        preparationHandler.promise().get_future().wait();

        TestHandlerCreateIteratorWithOpenedFile mainHandler(environment, driveKey);
        mainHandler.initiateSandboxModifications(
                "outer/inner/test.txt",
                {
                        {"outer/inner", false},
                        {"outer", false},
                        {"outer/sibling", true}},
                OpenFileMode::READ,
                false);
        mainHandler.promise().get_future().wait();
    }
} // namespace

#undef TEST_NAME
} // namespace sirius::drive::test

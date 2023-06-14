#include "TestEnvironment.h"
#include "drive/Utils.h"
#include "types.h"
#include "utils.h"
#include <fstream>
#include <numeric>

using namespace sirius::drive::test;

namespace sirius::drive::test {

    /// change this macro for your test
#define TEST_NAME SupercontractGetTwoUnavailableFilesAndCompareFiles

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

        class TestGetTwoUnavailableFilesAndCompare {
		public:
			std::promise<void> p;
			DriveKey m_driveKey;
			uint64_t m_fileId;
			uint64_t m_bytes;
            uint64_t m_size;
            uint64_t m_size2;
			ENVIRONMENT_CLASS& m_env;

			TestGetTwoUnavailableFilesAndCompare(ENVIRONMENT_CLASS& env)
				: m_env(env) {}

		public:
            void onReceivedFsTree(std::optional<FilesystemResponse> res) {
                ASSERT_TRUE(res);
                auto& fsTree = res->m_fsTree;
                ASSERT_TRUE(fsTree.childs().size() == 0);
				p.set_value();
            }

            void onAppliedStorageModifications(std::optional<ApplyStorageModificationsResponse> res) {
                ASSERT_TRUE(res);
                m_env.getFilesystem(m_driveKey, FilesystemRequest{ [this](auto res) {
                                        onReceivedFsTree(res);
                                    } });
            }

            void onStorageHashEvaluated(std::optional<EvaluateStorageHashResponse> res) {
                ASSERT_TRUE(res);
                m_env.applyStorageManualModifications(m_driveKey, ApplyStorageModificationsRequest{ true, [this](auto res) {
                                                                                                       onAppliedStorageModifications(res);
                                                                                                   } });
            }

            void onAppliedSandboxModifications(std::optional<ApplySandboxModificationsResponse> res) {
                ASSERT_TRUE(res);
                ASSERT_TRUE(res->m_success);
                m_env.evaluateStorageHash(m_driveKey, EvaluateStorageHashRequest{ [this](auto res) {
                                              onStorageHashEvaluated(res);
                                          } });
            }

			void onFileSize2(std::optional<FileSizeResponse> res) {
				ASSERT_TRUE(res);
				ASSERT_FALSE(res->m_success);
				m_env.applySandboxManualModifications(m_driveKey, ApplySandboxModificationsRequest{ true, [this](auto res) {
																		   onAppliedSandboxModifications(res);
																	   } });
			}	

			void onFileSize(std::optional<FileSizeResponse> res) {
				ASSERT_TRUE(res);
				ASSERT_FALSE(res->m_success);
				m_env.getSize(m_driveKey, FileSizeRequest{"test2.txt", [this](auto res) {
															   onFileSize2(res);
														   }});
			}

            void onSandboxModificationsInitiated(std::optional<InitiateSandboxModificationsResponse> res) {
                ASSERT_TRUE(res);
                m_env.getSize(m_driveKey, FileSizeRequest{"test.txt", [this](auto res) {
												   onFileSize(res);
											   }});
            }

            void onInitiatedModifications(std::optional<InitiateModificationsResponse> res) {
                ASSERT_TRUE(res);
                m_env.initiateManualSandboxModifications(m_driveKey, InitiateSandboxModificationsRequest{ [this](auto res) {
                                                             onSandboxModificationsInitiated(
                                                                 res);
                                                         } });
            }
		};

        TEST(SupercontractTest, TEST_NAME) {
            fs::remove_all(ROOT_FOLDER);
            EXLOG("");

            ENVIRONMENT_CLASS env(
                1, REPLICATOR_ADDRESS, PORT, DRIVE_ROOT_FOLDER,
                SANDBOX_ROOT_FOLDER, USE_TCP, 1, 1, 1024 * 1024);

            Key driveKey{ {1} };
            env.addDrive(driveKey, Key(), 100 * 1024 * 1024);

            TestGetTwoUnavailableFilesAndCompare handler(env);
            handler.m_driveKey = driveKey;
            env.initiateManualModifications(driveKey,
                InitiateModificationsRequest{ randomByteArray<Hash256>(), [&](auto res) { handler.onInitiatedModifications(res); } });

            handler.p.get_future().wait();
        }
    } // namespace

#undef TEST_NAME
} // namespace sirius::drive::test
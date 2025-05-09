#include "TestEnvironment.h"
#include "drive/Utils.h"
#include "types.h"
#include "utils.h"
#include <fstream>
#include <numeric>

using namespace sirius::drive::test;

namespace sirius::drive::test {

    /// change this macro for your test
#define TEST_NAME SupercontractCreateTwoFilesWithSameEmojiContentThenCompareFiles

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

        class TestCreateTwoFiles {

        public:
            std::promise<void> p;
            DriveKey m_driveKey;
            uint64_t m_fileId;
            ENVIRONMENT_CLASS& m_env;

            TestCreateTwoFiles(ENVIRONMENT_CLASS& env)
                : m_env(env) {}

        public:
            void onReceivedAbsolutePath2(std::optional<FileInfoResponse> res) {
                ASSERT_TRUE(res);
                std::ostringstream stream;
                const auto& path = res->m_path;
                ASSERT_TRUE(fs::exists(path));
                std::ifstream fileStream(path);
                stream << fileStream.rdbuf();
                auto content = stream.str();
                ASSERT_EQ(content, "🧪");
                p.set_value();
            }

            void onReceivedAbsolutePath(std::optional<FileInfoResponse> res) {
                ASSERT_TRUE(res);
                std::ostringstream stream;
                const auto& path = res->m_path;
                ASSERT_TRUE(fs::exists(path));
                std::ifstream fileStream(path);
                stream << fileStream.rdbuf();
                auto content = stream.str();
                ASSERT_EQ(content, "🧪");
                m_env.getAbsolutePath(m_driveKey, FileInfoRequest{ "test2.txt", [this](auto res) {
                                                                          onReceivedAbsolutePath2(res);
                                                                      } });
            }

            void onReceivedFsTree(std::optional<FilesystemResponse> res) {
                ASSERT_TRUE(res);
                auto& fsTree = res->m_fsTree;
                ASSERT_TRUE(fsTree.childs().size() == 2);
				for (auto const& [_, child] : fsTree.childs()) {
                    ASSERT_TRUE(isFile(child));
                    const auto& file = getFile(child);
                    if (file.name() == "test2.txt") {
                        ASSERT_TRUE(file.name() == "test2.txt");
                    }
                    else {
                        ASSERT_TRUE(file.name() == "test.txt");
                    }
                }
                m_env.getAbsolutePath(m_driveKey, FileInfoRequest{ "test.txt", [this](auto res) {
                                                                          onReceivedAbsolutePath(res);
                                                                      } });
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

            void onFileFlushed2(std::optional<FlushResponse> res) {
                ASSERT_TRUE(res);
                ASSERT_TRUE(res->m_success);
                m_env.applySandboxManualModifications(m_driveKey, ApplySandboxModificationsRequest{ true, [this](auto res) {
                                                                        onAppliedSandboxModifications(res);
                                                                    } });
            }

            void onFileWritten2(std::optional<WriteFileResponse> res) {
                ASSERT_TRUE(res);
                ASSERT_TRUE(res->m_success);
                m_env.flush(m_driveKey, FlushRequest{ m_fileId, [this](auto res) {
                                                         onFileFlushed2(res);
                                                     } });
            }

            void onFileOpened2(std::optional<OpenFileResponse> res) {
                ASSERT_TRUE(res);
                auto response = *res;
                ASSERT_TRUE(response.m_fileId);
                m_fileId = *response.m_fileId;
                std::string buffer = "🧪";
                m_env.writeFile(m_driveKey, WriteFileRequest{ m_fileId, {buffer.begin(), buffer.end()}, [this](auto res) {
                                                                 onFileWritten2(res);
                                                             } });
            }

            void onFileClosed(std::optional<CloseFileResponse> res) {
                ASSERT_TRUE(res);
                ASSERT_TRUE(res->m_success);
                m_env.openFile(m_driveKey, OpenFileRequest{ OpenFileMode::WRITE, "test2.txt", [this](auto res) { onFileOpened2(res); } });
            }

            void onFileFlushed(std::optional<FlushResponse> res) {
                ASSERT_TRUE(res);
                ASSERT_TRUE(res->m_success);
                m_env.closeFile(m_driveKey, CloseFileRequest{ m_fileId, [this](auto res) {
                                                                 onFileClosed(res);
                                                             } });
            }

            void onFileWritten(std::optional<WriteFileResponse> res) {
                ASSERT_TRUE(res);
                ASSERT_TRUE(res->m_success);
                m_env.flush(m_driveKey, FlushRequest{ m_fileId, [this](auto res) {
                                                         onFileFlushed(res);
                                                     } });
            }

            void onFileOpened(std::optional<OpenFileResponse> res) {
                ASSERT_TRUE(res);
                auto response = *res;
                ASSERT_TRUE(response.m_fileId);
                m_fileId = *response.m_fileId;
                std::string buffer = "🧪";
                m_env.writeFile(m_driveKey, WriteFileRequest{ m_fileId, {buffer.begin(), buffer.end()}, [this](auto res) {
                                                                 onFileWritten(res);
                                                             } });
            }

            void onSandboxModificationsInitiated(std::optional<InitiateSandboxModificationsResponse> res) {
                ASSERT_TRUE(res);
                m_env.openFile(m_driveKey, OpenFileRequest{ OpenFileMode::WRITE, "test.txt", [this](auto res) { onFileOpened(res); } });
            }

            void onInitiatedModifications(std::optional<InitiateModificationsResponse> res) {
                ASSERT_TRUE(res);
                m_env.initiateManualSandboxModifications(m_driveKey, InitiateSandboxModificationsRequest{ [this](auto res) {
                                                             onSandboxModificationsInitiated(
                                                                 res);
                                                         } });
            }
        };

        class TestCompareFileSize {
		public:
			std::promise<void> p;
			DriveKey m_driveKey;
			uint64_t m_fileId;
			uint64_t m_bytes;
            uint64_t m_size;
            uint64_t m_size2;
			ENVIRONMENT_CLASS& m_env;

			TestCompareFileSize(ENVIRONMENT_CLASS& env)
				: m_env(env) {}

		public:
			void onReceivedAbsolutePath2(std::optional<FileInfoResponse> res) {
                ASSERT_TRUE(res);
                std::ostringstream stream;
                const auto& path = res->m_path;
                ASSERT_TRUE(fs::exists(path));
                std::ifstream fileStream(path);
                stream << fileStream.rdbuf();
                auto content = stream.str();
                ASSERT_EQ(content, "🧪");
                p.set_value();
            }

            void onReceivedAbsolutePath(std::optional<FileInfoResponse> res) {
                ASSERT_TRUE(res);
                std::ostringstream stream;
                const auto& path = res->m_path;
                ASSERT_TRUE(fs::exists(path));
                std::ifstream fileStream(path);
                stream << fileStream.rdbuf();
                auto content = stream.str();
                ASSERT_EQ(content, "🧪");
                m_env.getAbsolutePath(m_driveKey, FileInfoRequest{ "test2.txt", [this](auto res) {
                                                                          onReceivedAbsolutePath2(res);
                                                                      } });
            }

            void onReceivedFsTree(std::optional<FilesystemResponse> res) {
                ASSERT_TRUE(res);
                auto& fsTree = res->m_fsTree;
                ASSERT_TRUE(fsTree.childs().size() == 2);
				for (auto const& [_, child] : fsTree.childs()) {
                    ASSERT_TRUE(isFile(child));
                    const auto& file = getFile(child);
                    if (file.name() == "test2.txt") {
                        ASSERT_TRUE(file.name() == "test2.txt");
                    }
                    else {
                        ASSERT_TRUE(file.name() == "test.txt");
                    }
                }
                m_env.getAbsolutePath(m_driveKey, FileInfoRequest{ "test.txt", [this](auto res) {
                                                                          onReceivedAbsolutePath(res);
                                                                      } });
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
                ASSERT_TRUE(res->m_success);
				FileSizeResponse response = *res;
				m_size2 = response.m_size;
				ASSERT_EQ(m_size, m_size2);
				m_env.applySandboxManualModifications(m_driveKey, ApplySandboxModificationsRequest{ true, [this](auto res) {
																		   onAppliedSandboxModifications(res);
																	   } });
			}

			void onFileSize(std::optional<FileSizeResponse> res) {
				ASSERT_TRUE(res);
                ASSERT_TRUE(res->m_success);
				FileSizeResponse response = *res;
				m_size = response.m_size;
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

            TestCreateTwoFiles handler(env);
            handler.m_driveKey = driveKey;
            env.initiateManualModifications(driveKey,
                InitiateModificationsRequest{ randomByteArray<Hash256>(), [&](auto res) { handler.onInitiatedModifications(res); } });

            handler.p.get_future().wait();

            TestCompareFileSize handler2(env);
            handler2.m_driveKey = driveKey;
            env.initiateManualModifications(driveKey,
                InitiateModificationsRequest{ randomByteArray<Hash256>(), [&](auto res) { handler2.onInitiatedModifications(res); } });

            handler2.p.get_future().wait();
        }
    } // namespace

#undef TEST_NAME
} // namespace sirius::drive::test


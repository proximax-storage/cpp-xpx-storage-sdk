#include "TestEnvironment.h"
#include "drive/Utils.h"
#include "types.h"
#include "utils.h"
#include <fstream>
#include <numeric>

using namespace sirius::drive::test;

namespace sirius::drive::test {

    /// change this macro for your test
#define TEST_NAME SupercontractCreateADirWithFileWithThreeSymbolSlashAsPathSeparatorThenMoveFileOutsideDirBackAndForthButSecondMoveWithOneSlashSymbolAsPathSeparator

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

        class TestCreateTwoDir {

        public:
            std::promise<void> p;
            DriveKey m_driveKey;
            uint64_t m_fileId;
            ENVIRONMENT_CLASS& m_env;

            TestCreateTwoDir(ENVIRONMENT_CLASS& env)
                : m_env(env) {}

        public:
            void onReceivedAbsolutePath(std::optional<FileInfoResponse> res) {
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
                m_env.getAbsolutePath(m_driveKey, FileInfoRequest{ "tests///test.txt", [this](auto res) {
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

            void onFileClosed(std::optional<CloseFileResponse> res) {
                ASSERT_TRUE(res);
                ASSERT_TRUE(res->m_success);
                m_env.applySandboxManualModifications(m_driveKey, ApplySandboxModificationsRequest{ true, [this](auto res) {
															   onAppliedSandboxModifications(res);
														   } });
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
                std::string buffer = "data";
                m_env.writeFile(m_driveKey, WriteFileRequest{ m_fileId, {buffer.begin(), buffer.end()}, [this](auto res) {
                                                                 onFileWritten(res);
                                                             } });
            }

            void onDirCreated(std::optional<CreateDirectoriesResponse> res) {
                ASSERT_TRUE(res);
                m_env.openFile(m_driveKey, OpenFileRequest{ OpenFileMode::WRITE, "tests///test.txt", [this](auto res) { onFileOpened(res); } });
            }

            void onSandboxModificationsInitiated(std::optional<InitiateSandboxModificationsResponse> res) {
                ASSERT_TRUE(res);
                m_env.createDirectories(m_driveKey, CreateDirectoriesRequest{ "tests", [this](auto res) { onDirCreated(res); } });
            }

            void onInitiatedModifications(std::optional<InitiateModificationsResponse> res) {
                ASSERT_TRUE(res);
                m_env.initiateManualSandboxModifications(m_driveKey, InitiateSandboxModificationsRequest{ [this](auto res) {
                                                             onSandboxModificationsInitiated(
                                                                 res);
                                                         } });
            }
        };

        class TestMoveBackAndForth {

        public:
            std::promise<void> p;
            DriveKey m_driveKey;
            uint64_t m_fileId;
            ENVIRONMENT_CLASS& m_env;

            TestMoveBackAndForth(ENVIRONMENT_CLASS& env)
                : m_env(env) {}

        public:
            void onReceivedAbsolutePath(std::optional<FileInfoResponse> res) {
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
                m_env.getAbsolutePath(m_driveKey, FileInfoRequest{ "tests/test.txt", [this](auto res) {
                                                                          onReceivedAbsolutePath(res);
                                                                      } });
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

			void onFileMoved2(std::optional<MoveFilesystemEntryResponse> res) {
                ASSERT_TRUE(res);
                ASSERT_TRUE(res->m_success);
				m_env.applySandboxManualModifications(m_driveKey, ApplySandboxModificationsRequest{ true, [this](auto res) {
														   onAppliedSandboxModifications(res);
													   } });
            }

			void onFileMoved(std::optional<MoveFilesystemEntryResponse> res) {
                ASSERT_TRUE(res);
                ASSERT_TRUE(res->m_success);
				m_env.moveFsTreeEntry(m_driveKey, MoveFilesystemEntryRequest{ "test.txt", "tests/test.txt", [this](auto res) { onFileMoved2(res); } });
            }

			void onSandboxModificationsInitiated(std::optional<InitiateSandboxModificationsResponse> res) {
				ASSERT_TRUE(res);
				m_env.moveFsTreeEntry(m_driveKey, MoveFilesystemEntryRequest{ "tests///test.txt", "test.txt", [this](auto res) { onFileMoved(res); } });
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

            Key driveKey{ {1} };
            env.addDrive(driveKey, Key(), 100 * 1024 * 1024);

            TestCreateTwoDir handler(env);
            handler.m_driveKey = driveKey;
            env.initiateManualModifications(driveKey,
                InitiateModificationsRequest{ randomByteArray<Hash256>(), [&](auto res) { handler.onInitiatedModifications(res); } });

            handler.p.get_future().wait();

            TestMoveBackAndForth handler2(env);
            handler2.m_driveKey = driveKey;
            env.initiateManualModifications(driveKey,
                InitiateModificationsRequest{ randomByteArray<Hash256>(), [&](auto res) { handler2.onInitiatedModifications(res); } });

            handler2.p.get_future().wait();
        }
    } // namespace

#undef TEST_NAME
} // namespace sirius::drive::test


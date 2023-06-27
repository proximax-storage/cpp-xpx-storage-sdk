#pragma once

#include <condition_variable>
#include <utility>
#include <drive/FsTree.h>
#include "types.h"
#include "drive/ClientSession.h"
#include "drive/Utils.h"
#include "crypto/Signer.h"
#include "gtest/gtest.h"

namespace sirius::drive::test
{

    namespace fs = std::filesystem;

    using namespace sirius::drive;

#define ROOT_FOLDER fs::path(getenv("HOME")) / "111"
#define NUMBER_OF_REPLICATORS 4
#define REPLICATOR_ADDRESS "192.168.2.1"
#define PORT 5550
#define DRIVE_ROOT_FOLDER (ROOT_FOLDER / "Drive")
#define SANDBOX_ROOT_FOLDER (ROOT_FOLDER / "Sandbox")
#define USE_TCP false

#define CLIENT_ADDRESS "192.168.2.200"
#define CLIENT_WORK_FOLDER (ROOT_FOLDER / "client_work_folder")
#define DRIVE_PUB_KEY std::array<uint8_t,32>{1,0,0,0,0,5,6,7,8,9, 0,1,2,3,4,5,6,7,8,9, 0,1,2,3,4,5,6,7,8,9, 0,1}
#define DRIVE_PUB_KEY_2 std::array<uint8_t,32>{2,0,0,0,0,5,6,7,8,9, 0,1,2,3,4,5,6,7,8,9, 0,1,2,3,4,5,6,7,8,9, 0,1}
#define BIG_FILE_SIZE (10 * 1024 * 1024)

#define JOIN(x, y) JOIN_AGAIN(x, y)
#define JOIN_AGAIN(x, y) x ## y

    extern std::mutex gExLogMutex;

#define EXLOG(expr) { \
__LOG( "+++ exlog: " << expr << std::endl << std::flush); \
}

    void clientSessionErrorHandler(const lt::alert *alert);

    class TestClient
    {
    public:
        sirius::crypto::KeyPair m_clientKeyPair;
        std::shared_ptr<ClientSession> m_clientSession;
        fs::path m_clientFolder;
        std::vector<InfoHash> m_actionListHashes;
        std::vector<sirius::Hash256> m_modificationTransactionHashes;

        std::vector<sirius::Hash256> m_downloadChannels;
        std::map<InfoHash, bool> m_downloadCompleted;
        std::map<Hash256, std::condition_variable> m_downloadCondVars;
        std::mutex m_downloadCompleteMutex;

        TestClient( const endpoint_list & bootstraps,
                    const lt::settings_pack &pack = lt::settings_pack(),
                    const fs::path& clientFolder = fs::path(".") / "111" / "client_drive" ) :
                m_clientKeyPair(sirius::crypto::KeyPair::FromPrivate(
                        sirius::crypto::PrivateKey::FromString(
                                "0000000000010203040501020304050102030405010203040501020304050102"))),
                m_clientSession(createClientSession(std::move(m_clientKeyPair),
                                                    CLIENT_ADDRESS ":5550",
                                                    clientSessionErrorHandler,
                                                    bootstraps,
                                                    USE_TCP,
                                                    "client")),
                m_clientFolder(clientFolder)
        {
            fs::remove_all(clientFolder);
            m_clientSession->setSessionSettings(pack, true);
        }

        ~TestClient()
        {
            m_clientSession->stop();
        }

        void modifyDrive(const ActionList &actionList,
                         const ReplicatorList &replicatorList,
                         const Key& drivePubKey)
        {
            actionList.dbgPrint();
            // Create empty tmp folder for 'client modify data'
            //
            auto tmpFolder = fs::temp_directory_path() / "modify_drive_data";
            // start file uploading
            uint64_t totalModifyDataSize;
            InfoHash hash = m_clientSession->addActionListToSession(actionList, drivePubKey, replicatorList, tmpFolder, totalModifyDataSize );

            // inform replicator
            m_actionListHashes.push_back(hash);
            m_modificationTransactionHashes.push_back(randomByteArray<Hash256>());

            EXLOG("# Client is waiting the end of replicator update");
        }

        void removeModifyTorrents() {
            m_clientSession->removeTorrents();
        }

        void downloadFromDrive(const InfoHash& rootHash,
                               const Key& downloadChannelKey,
                               const ReplicatorList &replicatorList)
        {
            auto downloadChannelId = Hash256(downloadChannelKey.array());
            m_clientSession->addDownloadChannel(downloadChannelId);
            m_clientSession->download(DownloadContext(
                                              DownloadContext::fs_tree,
                                              [this] (download_status::code code,
                                                 const InfoHash &infoHash,
                                                 const std::filesystem::path /*filePath*/,
                                                 size_t /*downloaded*/,
                                                 size_t /*fileSize*/,
                                                 const std::string & /*errorText*/ )
                                              {
                                                  std::unique_lock<std::mutex> lock(m_downloadCompleteMutex);
                                                  m_downloadCompleted[infoHash] = true;
                                                  m_downloadCondVars[infoHash].notify_all();
                                              },
                                              rootHash,
                                              downloadChannelId, 0),
                                      downloadChannelId, m_clientFolder / "fsTree-folder", "");
            m_downloadChannels.push_back(downloadChannelId);
        }

        auto getFsTreeFiles()
        {
            FsTree fsTree;
            fsTree.deserialize( m_clientFolder / "fsTree-folder" / FS_TREE_FILE_NAME );
            std::set<InfoHash> files;
            iterateFsTreeFiles(fsTree, files);
            return files;
        }

    private:

        void iterateFsTreeFiles(const Folder & folder, std::set<InfoHash> & files)
        {
            for (const auto& [name, child]: folder.childs())
            {
                if (isFolder(child))
                {
                    iterateFsTreeFiles(getFolder(child), files);
                }
                else
                {
                    files.insert(getFile(child).hash());
                }
            }
        }

    public:

//        void synchronizeDrive( const fs::path& baseFolder, const Folder& folder )
//        {
//            for( const auto& child: folder.childs() )
//            {
//                if ( isFolder(child) )
//                {
//                    const auto& childFolder = getFolder(child);
//                    synchronizeDrive( baseFolder / childFolder.name(), childFolder );
//                }
//                else
//                {
////                    m_clientSession->download( DownloadContext(
////                            DownloadContext::file_from_drive,
////                            clientDownloadFilesHandler,
////                            file.hash(),
////                            {}, 0,
////                            gClientFolder / "downloaded_files" / folderName / file.name() ),
////                                              //gClientFolder / "downloaded_files" / folderName / toString(file.hash()) ),
////                                              gClientFolder / "downloaded_files" );
//                }
//            }
//        }

        void waitForDownloadComplete(const InfoHash& infoHash) {
            std::unique_lock<std::mutex> lock(m_downloadCompleteMutex);
            m_downloadCondVars[infoHash].wait(lock, [this, infoHash] {
                return m_downloadCompleted[infoHash];
            });
        }
    };


    /// Handler that creates directories and files in \a environment with specified \a driveKey.
    template<typename TEnvironment>
    class TestHandlerPrepareFsTree {
    public:
        TestHandlerPrepareFsTree(
                TEnvironment& environment,
                DriveKey driveKey)
                : m_environment(environment)
                , m_driveKey(std::move(driveKey)) {}

    public:
        /// Gets promise.
        std::promise<void>& promise() {
            return m_promise;
        }

        /// Initiates a series of modifications that will create requested directories and files.
        /// \a requestedFsTree maps requested directories to files that should be created in those directories.
        /// Inner map of \a requestedFsTree maps names of requested files to their sizes (in bytes).
        void initiateManualModifications(
                const std::map<std::string, std::map<std::string, size_t>>& requestedFsTree,
                const bool applyStorageModifications = false) {
            for (const auto& [directoryAbsolutePath, filesMap] : requestedFsTree) {
                m_requestedDirectories.insert(directoryAbsolutePath);
                for (const auto& [fileName, fileSize] : filesMap) {
                    const bool isAtRoot = directoryAbsolutePath == "";
                    const auto fileAbsolutePath = isAtRoot ? fileName : (directoryAbsolutePath + "/" + fileName);
                    m_requestedFiles.emplace(fileAbsolutePath, fileSize);
                }
            }
            m_applyStorageModifications = applyStorageModifications;
            initiateManualModifications();
        }

        /// Initiates a series of modifications that will create requested files in root.
        /// \a requestedFiles maps names of requested files to their sizes (in bytes).
        void initiateManualModifications(
                const std::map<std::string, size_t>& requestedFiles,
                const bool applyStorageModifications = false) {
            for (const auto& [fileName, fileSize] : requestedFiles) {
                m_requestedFiles.emplace(fileName, fileSize);
            }
            m_applyStorageModifications = applyStorageModifications;
            initiateManualModifications();
        }

    private:
        void onStorageModificationsApplied(std::optional<ApplyStorageModificationsResponse> response) {
            ASSERT_TRUE(response);
            m_promise.set_value();
        }

        void onStorageHashEvaluated(std::optional<EvaluateStorageHashResponse> response) {
            ASSERT_TRUE(response);
            if (m_applyStorageModifications) {
                m_environment.applyStorageManualModifications(
                        m_driveKey,
                        ApplyStorageModificationsRequest{
                            true,
                            [this](auto response) { onStorageModificationsApplied(response); }
                        });
            } else {
                m_promise.set_value();
            }
        }

        void onSandboxModificationsApplied(std::optional<ApplySandboxModificationsResponse> response) {
            ASSERT_TRUE(response);
            ASSERT_TRUE(response->m_success);
            m_environment.evaluateStorageHash(
                    m_driveKey,
                    EvaluateStorageHashRequest{
                        [this](auto response) { onStorageHashEvaluated(response); }
                    });
        }

        void onFileClosed(std::optional<CloseFileResponse> response) {
            ASSERT_TRUE(response);
            ASSERT_TRUE(response->m_success);
            ++m_fileIter;
            maybeOpenFile();
        }

        void onFileFlushed(std::optional<FlushResponse> response) {
            ASSERT_TRUE(response);
            ASSERT_TRUE(response->m_success);
            m_environment.closeFile(
                    m_driveKey,
                    CloseFileRequest{
                        m_fileId,
                        [this](auto response) { onFileClosed(response); }
                    });
        }

        void onFileWritten(std::optional<WriteFileResponse> response) {
            ASSERT_TRUE(response);
            ASSERT_TRUE(response->m_success);
            m_environment.flush(
                    m_driveKey,
                    FlushRequest{
                        m_fileId,
                        [this](auto response) { onFileFlushed(response); }
                    });
        }

        void onFileOpened(std::optional<OpenFileResponse> response) {
            ASSERT_TRUE(response);
            auto responseValue = *response;
            ASSERT_TRUE(responseValue.m_fileId);
            m_fileId = *responseValue.m_fileId;

            const auto& fileSize = m_fileIter->second;
            std::vector<uint8_t> buffer(fileSize);
            for (auto& byte : buffer)
                byte = static_cast<uint8_t>(rand() % 256);

            m_environment.writeFile(
                    m_driveKey,
                    WriteFileRequest{
                        m_fileId,
                        std::move(buffer),
                        [this](auto response) { onFileWritten(response); }
                    });
        }

        void maybeOpenFile() {
            if (m_fileIter != m_requestedFiles.end()) {
                const auto& fileAbsolutePath = m_fileIter->first;
                m_environment.openFile(
                        m_driveKey,
                        OpenFileRequest{
                            OpenFileMode::WRITE,
                            fileAbsolutePath,
                            [this](auto response) { onFileOpened(response); }
                        });
            } else {
                m_environment.applySandboxManualModifications(
                        m_driveKey,
                        ApplySandboxModificationsRequest{
                            true,
                            [this](auto response) { onSandboxModificationsApplied(response); }
                        });
            }
        }

        void onDirectoryCreated(std::optional<CreateDirectoriesResponse> response) {
            ASSERT_TRUE(response);
            ++m_directoryIter;
            maybeCreateDirectory();
        }

        void maybeCreateDirectory() {
            if (m_directoryIter != m_requestedDirectories.end()) {
                m_environment.createDirectories(
                        m_driveKey,
                        CreateDirectoriesRequest{
                            *m_directoryIter,
                            [this](auto response) { onDirectoryCreated(response); }
                        });
            } else {
                maybeOpenFile();
            }
        }

        void onSandboxModificationsInitiated(std::optional<InitiateSandboxModificationsResponse> response) {
            ASSERT_TRUE(response);
            maybeCreateDirectory();
        }

        void onManualModificationsInitiated(std::optional<InitiateModificationsResponse> response) {
            ASSERT_TRUE(response);
            m_environment.initiateManualSandboxModifications(
                    m_driveKey,
                    InitiateSandboxModificationsRequest{
                        [this](auto response) { onSandboxModificationsInitiated(response); }
                    });
        }

        void initiateManualModifications() {
            m_directoryIter = m_requestedDirectories.begin();
            m_fileIter = m_requestedFiles.begin();
            m_environment.initiateManualModifications(
                    m_driveKey,
                    InitiateModificationsRequest{
                            randomByteArray<Hash256>(),
                            [&](auto response) { onManualModificationsInitiated(response); }
                    });
        }

    private:
        std::promise<void> m_promise;
        TEnvironment& m_environment;
        DriveKey m_driveKey;
        std::set<std::string> m_requestedDirectories;
        std::map<std::string, size_t> m_requestedFiles;
        bool m_applyStorageModifications;
        std::set<std::string>::iterator m_directoryIter;
        std::map<std::string, size_t>::iterator m_fileIter;
        uint64_t m_fileId;
    };

    /// Some Functions For Tests
    fs::path createClientFiles(const fs::path &clientFolder, size_t bigFileSize);

    ActionList createActionList(const fs::path &clientRootFolder);

    ActionList createActionList(const fs::path &clientRootFolder, uint64_t size);

    ActionList createActionList_2(const fs::path &clientRootFolder);

    ActionList createActionList_3(const fs::path &clientRootFolder);
}

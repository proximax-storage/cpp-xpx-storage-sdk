#pragma once

#include <condition_variable>
#include <drive/FsTree.h>
#include "types.h"
#include "../../src/drive/Session.h"
#include "drive/ClientSession.h"
#include "drive/Utils.h"
#include "crypto/Signer.h"

namespace sirius::drive::test
{

    namespace fs = std::filesystem;

    using namespace sirius::drive;

#define ROOT_FOLDER fs::path(getenv("HOME")) / "111"
#define NUMBER_OF_REPLICATORS 4
#define REPLICATOR_ADDRESS "192.168.2.1"
#define PORT 5500
#define DRIVE_ROOT_FOLDER (ROOT_FOLDER / "Drive")
#define SANDBOX_ROOT_FOLDER (ROOT_FOLDER / "Sandbox")
#define USE_TCP false

#define CLIENT_ADDRESS "192.168.2.200"
#define CLIENT_WORK_FOLDER (ROOT_FOLDER / "client_work_folder")
#define DRIVE_PUB_KEY std::array<uint8_t,32>{1,0,0,0,0,5,6,7,8,9, 0,1,2,3,4,5,6,7,8,9, 0,1,2,3,4,5,6,7,8,9, 0,1}
#define BIG_FILE_SIZE (10 * 1024 * 1024)

#define JOIN(x, y) JOIN_AGAIN(x, y)
#define JOIN_AGAIN(x, y) x ## y

    extern std::mutex gExLogMutex;

#define EXLOG(expr) { \
const std::lock_guard<std::mutex> autolock( gExLogMutex ); \
std::cout << now_str() << ": " << expr << std::endl << std::flush; \
}

    std::string now_str();

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
            InfoHash hash = m_clientSession->addActionListToSession(actionList, drivePubKey, replicatorList, tmpFolder);

            // inform replicator
            m_actionListHashes.push_back(hash);
            m_modificationTransactionHashes.push_back(randomByteArray<Hash256>());

            EXLOG("# Client is waiting the end of replicator update");
        }

        void downloadFromDrive(const InfoHash& rootHash,
                               const Key& downloadChannelKey,
                               const ReplicatorList &replicatorList)
        {
            auto downloadChannelId = Hash256(downloadChannelKey.array());
            m_clientSession->setDownloadChannel(replicatorList, downloadChannelId);
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
                                      m_clientFolder / "fsTree-folder");
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
            for (const auto& child: folder.childs())
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

    /// Some Functions For Tests
    fs::path createClientFiles(const fs::path &clientFolder, size_t bigFileSize);

    ActionList createActionList(const fs::path &clientRootFolder);

    ActionList createActionList_2(const fs::path &clientRootFolder);

    ActionList createActionList_3(const fs::path &clientRootFolder);
}

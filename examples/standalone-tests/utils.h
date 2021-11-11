#pragma once

#include "types.h"
#include "drive/Session.h"
#include "drive/ClientSession.h"
#include "drive/Utils.h"
#include "crypto/Signer.h"

namespace sirius::drive::test {

namespace fs = std::filesystem;

using namespace sirius::drive;

#define NUMBER_OF_REPLICATORS 4
#define REPLICATOR_ADDRESS "192.168.2.1"
#define PORT 5550
#define DRIVE_ROOT_FOLDER (ROOT_FOLDER / "Drive")
#define SANDBOX_ROOT_FOLDER (ROOT_FOLDER / "Sandbox")
#define USE_TCP false

#define ROOT_FOLDER fs::path(getenv("HOME")) / "111"
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

    template<class T>
    T randomByteArray() {
        T data;
        for (auto it = data.begin(); it != data.end(); it++) {
            *it = static_cast<uint8_t>(rand() % 256);
        }
        return data;
    }

    void clientSessionErrorHandler(const lt::alert *alert);

    class TestClient {
    public:
        fs::path clientFolder;
        sirius::crypto::KeyPair m_clientKeyPair;
        std::shared_ptr<ClientSession> m_clientSession;
        std::vector<InfoHash> m_actionListHashes;
        std::vector<sirius::Hash256> m_modificationTransactionHashes;

        TestClient(const lt::settings_pack& pack = lt::settings_pack()) :
                m_clientKeyPair(sirius::crypto::KeyPair::FromPrivate(
                        sirius::crypto::PrivateKey::FromString(
                                "0000000000010203040501020304050102030405010203040501020304050102"))),
                m_clientSession(createClientSession(std::move(m_clientKeyPair),
                                CLIENT_ADDRESS ":5550",
                                clientSessionErrorHandler,
                                USE_TCP,
                                "client"))
        {
            m_clientSession->setSessionSettings(pack, true);
        }

        void modifyDrive(const ActionList &actionList,
                               const ReplicatorList &replicatorList) {
            actionList.dbgPrint();
            auto transactionHash = randomByteArray<Hash256>();
            // Create empty tmp folder for 'client modify data'
            //
            auto tmpFolder = fs::temp_directory_path() / "modify_drive_data";
            // start file uploading
            InfoHash hash = m_clientSession->addActionListToSession(actionList, replicatorList, transactionHash, tmpFolder);

            // inform replicator
            m_actionListHashes.push_back(hash);
            m_modificationTransactionHashes.push_back(randomByteArray<Hash256>());

            EXLOG("# Client is waiting the end of replicator update");
        }
    };

    /// Some Functions For Tests
    fs::path createClientFiles(const fs::path& clientFolder, size_t bigFileSize );
    ActionList createActionList(const fs::path& clientRootFolder);
}
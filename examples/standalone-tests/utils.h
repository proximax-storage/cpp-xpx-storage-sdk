#include <drive/RpcReplicatorClient.h>

#include "types.h"
#include "drive/Session.h"
#include "drive/ClientSession.h"
#include "drive/Utils.h"
#include "crypto/Signer.h"

namespace sirius::drive::test {

#define NUMBER_OF_REPLICATORS 4
#define REPLICATOR_ADDRESS "192.168.2.1"
#define PORT 5550
#define DRIVE_ROOT_FOLDER (ROOT_FOLDER / "Drive")
#define SANDBOX_ROOT_FOLDER (ROOT_FOLDER / "Sandbox")
#define USE_TCP false

#define ROOT_FOLDER fs::path(getenv("HOME")) / "111"
#define CLIENT_ADDRESS "192.168.2.200"
#define CLIENT_WORK_FOLDER (ROOT_FOLDER / "client_work_folder")

    extern sirius::crypto::KeyPair clientKeyPair;

#define DRIVE_PUB_KEY std::array<uint8_t,32>{1,0,0,0,0,5,6,7,8,9, 0,1,2,3,4,5,6,7,8,9, 0,1,2,3,4,5,6,7,8,9, 0,1}
#define BIG_FILE_SIZE 10 * 1024 * 1024

    extern std::mutex gExLogMutex;

#define EXLOG(expr) { \
const std::lock_guard<std::mutex> autolock( gExLogMutex ); \
std::cout << now_str() << ": " << expr << std::endl << std::flush; \
}

    namespace fs = std::filesystem;

    using namespace sirius::drive;

    fs::path createClientFiles(size_t bigFileSize);

    void clientModifyDrive(const ActionList &actionList,
                           const ReplicatorList &replicatorList,
                           const sirius::Hash256 &transactionHash);

    std::string now_str();

    ActionList createActionList();

    void clientSessionErrorHandler(const lt::alert *alert);

    extern fs::path gClientFolder;
    extern std::shared_ptr<ClientSession> gClientSession;
    extern const sirius::Hash256 modifyTransactionHash1;
    extern InfoHash clientModifyHash;
}
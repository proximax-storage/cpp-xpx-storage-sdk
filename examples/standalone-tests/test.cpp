#include <drive/RpcReplicatorClient.h>
#include <drive/ExtensionEmulator.h>
#include "TestEnvironment.h"

#include "types.h"
#include "drive/Session.h"
#include "drive/ClientSession.h"
#include "drive/Replicator.h"
#include "drive/FlatDrive.h"
#include "drive/FsTree.h"
#include "drive/Utils.h"
#include "crypto/Signer.h"

#define NUMBER_OF_REPLICATORS 4
#define REPLICATOR_ADDRESS "192.168.2.1"
#define PORT 5550
#define ROOT_FOLDER fs::path(getenv("HOME")) / "111"
#define DRIVE_ROOT_FOLDER (ROOT_FOLDER / "Drive")
#define SANDBOX_ROOT_FOLDER (ROOT_FOLDER / "Sandbox")
#define USE_TCP false

#define CLIENT_ADDRESS "192.168.2.200"
#define CLIENT_WORK_FOLDER (ROOT_FOLDER / "client_work_folder")

auto clientKeyPair = sirius::crypto::KeyPair::FromPrivate(
        sirius::crypto::PrivateKey::FromString("0000000000010203040501020304050102030405010203040501020304050102"));

#define DRIVE_PUB_KEY std::array<uint8_t,32>{1,0,0,0,0,5,6,7,8,9, 0,1,2,3,4,5,6,7,8,9, 0,1,2,3,4,5,6,7,8,9, 0,1}
#define BIG_FILE_SIZE 10 * 1024 * 1024

namespace fs = std::filesystem;

using namespace sirius::drive;

static fs::path createClientFiles(size_t bigFileSize);

static void clientModifyDrive(const ActionList &actionList,
                              const ReplicatorList &replicatorList,
                              const sirius::Hash256 &transactionHash);

// Client folder for his files
fs::path gClientFolder;

// Libtorrent session
std::shared_ptr<ClientSession> gClientSession;

const sirius::Hash256 modifyTransactionHash1 = std::array<uint8_t,32>{0xa1,0xf,0xf,0xf};
InfoHash clientModifyHash;

// Listen (socket) error handle
//
static void clientSessionErrorHandler(const lt::alert *alert) {
    if (alert->type() == lt::listen_failed_alert::alert_type) {
        std::cerr << alert->message() << std::endl << std::flush;
        exit(-1);
    }
}

#ifdef __APPLE__
#pragma mark --MyReplicatorEventHandler--
#endif

#ifdef __APPLE__
#pragma mark --main()--
#endif

class IgnoreOpinionTestEnvironment: public TestEnvironment {
public:
    IgnoreOpinionTestEnvironment(
        int numberOfReplicators,
        const std::string &ipAddr0,
        int port0,
        const std::string &rootFolder0,
        const std::string &sandboxRootFolder0,
        bool useTcpSocket,
        int modifyApprovalDelay,
        int downloadApprovalDelay,
        bool startReplicator=true)
        : TestEnvironment(
                numberOfReplicators,
                ipAddr0,
                port0,
                rootFolder0,
                sandboxRootFolder0,
                useTcpSocket,
                modifyApprovalDelay,
                downloadApprovalDelay,
                startReplicator)
        {}

    void modifyApprovalTransactionIsReady(Replicator &replicator, ApprovalTransactionInfo &&transactionInfo) override {
//        transactionInfo.m_opinions.pop_back();
        TestEnvironment::modifyApprovalTransactionIsReady(replicator, std::move(transactionInfo));
    }
};

//
// main
//
int main(int, char **) {
    fs::remove_all(ROOT_FOLDER);

    auto startTime = std::clock();

    gClientFolder = createClientFiles(BIG_FILE_SIZE);
    gClientSession = createClientSession(std::move(clientKeyPair),
                                         CLIENT_ADDRESS ":5550",
                                         clientSessionErrorHandler,
                                         USE_TCP,
                                         "client" );
    _LOG("");

    fs::path clientFolder = gClientFolder / "client_files";
    IgnoreOpinionTestEnvironment env(
            NUMBER_OF_REPLICATORS, REPLICATOR_ADDRESS, PORT, DRIVE_ROOT_FOLDER,
            SANDBOX_ROOT_FOLDER, USE_TCP, 1, 1);

    EXLOG("\n# Client started: 1-st upload");
    {
        ActionList actionList;
        actionList.push_back(Action::newFolder("fff1/"));
        actionList.push_back(Action::newFolder("fff1/ffff1"));
        actionList.push_back(Action::upload(clientFolder / "a.txt", "fff2/a.txt"));

        //actionList.push_back( Action::upload( clientFolder / "a.txt", "a.txt" ) );
        actionList.push_back(Action::upload(clientFolder / "a.txt", "a2.txt"));
        actionList.push_back(Action::upload(clientFolder / "b.bin", "f1/b1.bin"));
        actionList.push_back(Action::upload(clientFolder / "b.bin", "f2/b2.bin"));
        actionList.push_back(Action::upload(clientFolder / "a.txt", "f2/a.txt"));

        clientModifyDrive(actionList, env.m_addrList, modifyTransactionHash1);
    }

    env.addDrive(DRIVE_PUB_KEY, 100 * 1024 * 1024);
    env.modifyDrive(DRIVE_PUB_KEY, {clientModifyHash, modifyTransactionHash1, BIG_FILE_SIZE + 1024, env.m_addrList, clientKeyPair.publicKey()});

    _LOG("\ntotal time: " << float(std::clock() - startTime) / CLOCKS_PER_SEC);
    int a;
    std::cin >> a;
    return 0;
}

//
// replicator
//
#ifdef __APPLE__
#pragma mark --replicator--
#endif

//
// clientModifyDrive
//
static void clientModifyDrive(const ActionList &actionList,
                              const ReplicatorList &replicatorList,
                              const sirius::Hash256 &transactionHash) {
    actionList.dbgPrint();

    // Create empty tmp folder for 'client modify data'
    //
    auto tmpFolder = fs::temp_directory_path() / "modify_drive_data";
    fs::remove_all(tmpFolder);
    fs::create_directories(tmpFolder);

    // start file uploading
    InfoHash hash = gClientSession->addActionListToSession(actionList, replicatorList, transactionHash, tmpFolder);

    // inform replicator
    clientModifyHash = hash;

    EXLOG("# Client is waiting the end of replicator update");
}

//
// createClientFiles
//
static fs::path createClientFiles( size_t bigFileSize ) {

    // Create empty tmp folder for testing
    //
    auto dataFolder = CLIENT_WORK_FOLDER / "client_files";
    fs::remove_all( dataFolder.parent_path() );
    fs::create_directories( dataFolder );
    //fs::create_directories( dataFolder/"empty_folder" );

    {
        std::ofstream file( dataFolder / "a.txt" );
        file.write( "a_txt", 5 );
    }
    {
        fs::path b_bin = dataFolder / "b.bin";
        fs::create_directories( b_bin.parent_path() );
        //        std::vector<uint8_t> data(10*1024*1024);
        std::vector<uint8_t> data(bigFileSize);
        std::generate( data.begin(), data.end(), std::rand );
        std::ofstream file( b_bin );
        file.write( (char*) data.data(), data.size() );
    }
    {
        std::ofstream file( dataFolder / "c.txt" );
        file.write( "c_txt", 5 );
    }
    {
        std::ofstream file( dataFolder / "d.txt" );
        file.write( "d_txt", 5 );
    }

    // Return path to file
    return dataFolder.parent_path();
}
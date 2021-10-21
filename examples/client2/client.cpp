#include "drive/RpcReplicatorClient.h"
#include "drive/ClientSession.h"
#include <filesystem>
#include <array>

using namespace sirius::drive;

int main() {
    const std::string clientPrivateKey = "0000000000010203040501020304050102030405010203040501020304050102";
    const std::string remoteRpcAddress = "192.168.0.101";
    const int remoteRpcPort = 5510;
    const std::string incomingAddress = "192.168.0.100";
    const int incomingPort = 5550;
    const std::filesystem::path workFolder = std::filesystem::path(getenv("HOME")) / "111" / "client_work_folder";

    RpcReplicatorClient rpcReplicatorClient(clientPrivateKey, remoteRpcAddress, remoteRpcPort, incomingAddress, incomingPort, workFolder, "client1");
    rpcReplicatorClient.createClientFiles(10 * 1024*1024);

    const std::array<uint8_t,32> drivePubKey{0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0, 0,1};
    rpcReplicatorClient.addDrive(drivePubKey, 100*1024*1024);

    ActionList actionList;
    actionList.push_back( Action::newFolder( "fff1/" ) );
    actionList.push_back( Action::newFolder( "fff1/ffff1" ) );
    actionList.push_back( Action::upload( workFolder / "client_files" / "a.txt", "a.txt" ) );
    actionList.push_back( Action::upload( workFolder / "client_files" / "a.txt", "a2.txt" ) );
    actionList.push_back( Action::upload( workFolder / "client_files" / "b.bin", "f1/b1.bin" ) );
    actionList.push_back( Action::upload( workFolder / "client_files" / "b.bin", "f2/b2.bin" ) );
    actionList.push_back( Action::upload( workFolder / "client_files" / "a.txt", "f2/a.txt" ) );

    const std::array<uint8_t,32> modifyTransactionHash{1,0,0,0,0,5,6,7,8,9, 0,1,2,3,4,5,6,7,8,9, 0,1,2,3,4,5,6,7,1,1,1,1};
    const uint64_t maxDataSize = 10;

    rpcReplicatorClient.modifyDrive(drivePubKey, actionList, modifyTransactionHash, maxDataSize);

    char a;
    int ret = std::scanf("%c\n", &a);
    (void) ret; // ignore

    return 0;
}
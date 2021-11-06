#include "drive/RpcReplicatorClient.h"
#include "drive/RpcReplicator.h"
#include "drive/ClientSession.h"
#include "drive/ExtensionEmulator.h"
#include <filesystem>
#include <array>

using namespace sirius::drive;

int main() {
    // Run emulator
    std::thread emulator([]{
        ExtensionEmulator extensionEmulator("192.168.2.101", 5510);
        extensionEmulator.run();
    });
    emulator.detach();

    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Run replicators
    std::thread replicator1([]{
        RpcReplicator replicator( "1000000000000000000000000000000000000000000000000000000000000000",
                                  "replicator1",
                                  "192.168.2.102",
                                  "5550",
                                  (std::string(getenv("HOME"))+"/111/replicator1/replicator_root"),
                                  (std::string(getenv("HOME"))+"/111/replicator1/sandbox_root"),
                                  5510,
                                  "192.168.2.101",
                                  5510);
        replicator.setModifyApprovalTransactionTimerDelay(1);
        replicator.setDownloadApprovalTransactionTimerDelay(1);
        replicator.run();
    });
    replicator1.detach();

    std::thread replicator2([]{
        RpcReplicator replicator( "2000000000000000000000000000000000000000000000000000000000000000",
                                  "replicator2",
                                  "192.168.2.103",
                                  "5550",
                                  (std::string(getenv("HOME"))+"/111/replicator2/replicator_root"),
                                  (std::string(getenv("HOME"))+"/111/replicator2/sandbox_root"),
                                  5510,
                                  "192.168.2.101",
                                  5510);
        replicator.setModifyApprovalTransactionTimerDelay(1);
        replicator.setDownloadApprovalTransactionTimerDelay(1);
        replicator.run();
    });
    replicator2.detach();

    std::thread replicator3([]{
        RpcReplicator replicator( "3000000000000000000000000000000000000000000000000000000000000000",
                                  "replicator3",
                                  "192.168.2.104",
                                  "5550",
                                  (std::string(getenv("HOME"))+"/111/replicator3/replicator_root"),
                                  (std::string(getenv("HOME"))+"/111/replicator3/sandbox_root"),
                                  5510,
                                  "192.168.2.101",
                                  5510);
        replicator.setModifyApprovalTransactionTimerDelay(1);
        replicator.setDownloadApprovalTransactionTimerDelay(1);
        replicator.run();
    });
    replicator3.detach();

    std::thread replicator4([]{
        RpcReplicator replicator( "4000000000000000000000000000000000000000000000000000000000000000",
                                  "replicator4",
                                  "192.168.2.105",
                                  "5550",
                                  (std::string(getenv("HOME"))+"/111/replicator4/replicator_root"),
                                  (std::string(getenv("HOME"))+"/111/replicator4/sandbox_root"),
                                  5510,
                                  "192.168.2.101",
                                  5510);
        replicator.setModifyApprovalTransactionTimerDelay(1);
        replicator.setDownloadApprovalTransactionTimerDelay(1);
        replicator.run();
    });
    replicator4.detach();

    std::thread replicator5([]{
        RpcReplicator replicator( "5000000000000000000000000000000000000000000000000000000000000000",
                                  "replicator5",
                                  "192.168.2.106",
                                  "5550",
                                  (std::string(getenv("HOME"))+"/111/replicator5/replicator_root"),
                                  (std::string(getenv("HOME"))+"/111/replicator5/sandbox_root"),
                                  5510,
                                  "192.168.2.101",
                                  5510);
        replicator.setModifyApprovalTransactionTimerDelay(1);
        replicator.setDownloadApprovalTransactionTimerDelay(1);
        replicator.run();
    });
    replicator5.detach();

    std::this_thread::sleep_for(std::chrono::seconds(3));

    // Client
    std::thread client([]{
        const std::string clientPrivateKey = "0000000000010203040501020304050102030405010203040501020304050102";
        const std::string remoteRpcAddress = "192.168.2.101";
        const int remoteRpcPort = 5510;
        const std::string incomingAddress = "192.168.2.100";
        const int incomingPort = 5550;
        const int incomingRpcPort = 5510;
        const std::filesystem::path workFolder = std::filesystem::path(getenv("HOME")) / "111" / "client_work_folder";

        RpcReplicatorClient rpcReplicatorClient(clientPrivateKey, remoteRpcAddress, remoteRpcPort, incomingAddress, incomingPort, incomingRpcPort, workFolder, "client1");
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

        auto endDriveModificationCallback = [&rpcReplicatorClient, drivePubKey]() {
            std::cout << "Client. endDriveModificationCallback." << std::endl;

            const std::array<uint8_t,32> channelKey{0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0, 0,123};
            const size_t prepaidDownloadSize = 10 * 1024*1024;

            rpcReplicatorClient.openDownloadChannel(channelKey, prepaidDownloadSize, drivePubKey, {rpcReplicatorClient.getPubKey()});

            auto downloadDataCallback = [](download_status::code code,
                                           const InfoHash& infoHash,
                                           const std::filesystem::path filePath,
                                           size_t downloaded,
                                           size_t fileSize,
                                           const std::string& errorText){
                if ( code == download_status::complete )
                {
                    std::cout << "Client. downloadDataCallback - COMPLETE: " << filePath.string() << " downloaded: " << downloaded <<
                    " fileSize: " << fileSize << " errorText: " << errorText <<std::endl;
                }
                else if ( code == download_status::failed )
                {
                    std::cout << "Client. downloadDataCallback - FAILED." << std::endl;
                }
            };

            auto downloadFsTreeCallback = [&rpcReplicatorClient, downloadDataCallback](const FsTree& fsTree, download_status::code code){
                if ( code == download_status::complete )
                {
                    std::cout << "Client. downloadFsTreeCallback - COMPLETE." << std::endl;
                    rpcReplicatorClient.downloadData(fsTree, downloadDataCallback);
                }
                else if ( code == download_status::failed )
                {
                    std::cout << "Client. downloadFsTreeCallback - FAILED." << std::endl;
                }
            };

            rpcReplicatorClient.downloadFsTree(drivePubKey, channelKey, downloadFsTreeCallback);
        };

        rpcReplicatorClient.modifyDrive(drivePubKey, actionList, modifyTransactionHash, maxDataSize, endDriveModificationCallback);
        rpcReplicatorClient.wait();
    });
    client.detach();

    char a;
    int ret = std::scanf("%c\n", &a);
    (void) ret; // ignore

    return 0;
}
#include "emulator/RpcReplicatorClient.h"
#include "emulator/RpcReplicator.h"
#include "drive/ClientSession.h"
#include "emulator/ExtensionEmulator.h"
#include <filesystem>
#include <array>

using namespace sirius::emulator;
using namespace sirius::drive;

int main() {
    // Run emulator
    std::thread emulator([]{
        ExtensionEmulator extensionEmulator("192.168.0.101", 5510);
        extensionEmulator.run();
    });
    emulator.detach();

    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Run replicators
    std::thread replicator1([]{
        const std::string replicatorPrivateKey = "1000000000000000000000000000000000000000000000000000000000000000";
        const auto keyPair = sirius::crypto::KeyPair::FromPrivate(sirius::crypto::PrivateKey::FromString( replicatorPrivateKey ));
        RpcReplicator replicator( keyPair,
                                  "replicator1",
                                  "192.168.0.102",
                                  "5550",
                                  (std::string(getenv("HOME"))+"/111/replicator1/replicator_root"),
                                  (std::string(getenv("HOME"))+"/111/replicator1/sandbox_root"),
                                  5510,
                                  "192.168.0.101",
                                  5510);
        replicator.setModifyApprovalTransactionTimerDelay(1);
        replicator.setDownloadApprovalTransactionTimerDelay(1);
        replicator.run();
    });
    replicator1.detach();

//    std::thread replicator2([]{
//        const std::string replicatorPrivateKey = "2000000000000000000000000000000000000000000000000000000000000000";
//        const auto keyPair = sirius::crypto::KeyPair::FromPrivate(sirius::crypto::PrivateKey::FromString( replicatorPrivateKey ));
//        RpcReplicator replicator( keyPair,
//                                  "replicator2",
//                                  "192.168.0.103",
//                                  "5550",
//                                  (std::string(getenv("HOME"))+"/111/replicator2/replicator_root"),
//                                  (std::string(getenv("HOME"))+"/111/replicator2/sandbox_root"),
//                                  5510,
//                                  "192.168.0.101",
//                                  5510);
//        replicator.setModifyApprovalTransactionTimerDelay(1);
//        replicator.setDownloadApprovalTransactionTimerDelay(1);
//        replicator.run();
//    });
//    replicator2.detach();
//
//    std::thread replicator3([]{
//        const std::string replicatorPrivateKey = "3000000000000000000000000000000000000000000000000000000000000000";
//        const auto keyPair = sirius::crypto::KeyPair::FromPrivate(sirius::crypto::PrivateKey::FromString( replicatorPrivateKey ));
//        RpcReplicator replicator( keyPair,
//                                  "replicator3",
//                                  "192.168.0.104",
//                                  "5550",
//                                  (std::string(getenv("HOME"))+"/111/replicator3/replicator_root"),
//                                  (std::string(getenv("HOME"))+"/111/replicator3/sandbox_root"),
//                                  5510,
//                                  "192.168.0.101",
//                                  5510);
//        replicator.setModifyApprovalTransactionTimerDelay(1);
//        replicator.setDownloadApprovalTransactionTimerDelay(1);
//        replicator.run();
//    });
//    replicator3.detach();
//
//    std::thread replicator4([]{
//        const std::string replicatorPrivateKey = "4000000000000000000000000000000000000000000000000000000000000000";
//        const auto keyPair = sirius::crypto::KeyPair::FromPrivate(sirius::crypto::PrivateKey::FromString( replicatorPrivateKey ));
//        RpcReplicator replicator( keyPair,
//                                  "replicator4",
//                                  "192.168.0.105",
//                                  "5550",
//                                  (std::string(getenv("HOME"))+"/111/replicator4/replicator_root"),
//                                  (std::string(getenv("HOME"))+"/111/replicator4/sandbox_root"),
//                                  5510,
//                                  "192.168.2.101",
//                                  5510);
//        replicator.setModifyApprovalTransactionTimerDelay(1);
//        replicator.setDownloadApprovalTransactionTimerDelay(1);
//        replicator.run();
//    });
//    replicator4.detach();
//
//    std::thread replicator5([]{
//        const std::string replicatorPrivateKey = "5000000000000000000000000000000000000000000000000000000000000000";
//        const auto keyPair = sirius::crypto::KeyPair::FromPrivate(sirius::crypto::PrivateKey::FromString( replicatorPrivateKey ));
//        RpcReplicator replicator( keyPair,
//                                  "replicator5",
//                                  "192.168.0.106",
//                                  "5550",
//                                  (std::string(getenv("HOME"))+"/111/replicator5/replicator_root"),
//                                  (std::string(getenv("HOME"))+"/111/replicator5/sandbox_root"),
//                                  5510,
//                                  "192.168.0.101",
//                                  5510);
//        replicator.setModifyApprovalTransactionTimerDelay(1);
//        replicator.setDownloadApprovalTransactionTimerDelay(1);
//        replicator.run();
//    });
//    replicator5.detach();

    std::this_thread::sleep_for(std::chrono::seconds(3));

    // Client
    std::thread client([]{
        const std::string clientPrivateKey = "0000000000010203040501020304050102030405010203040501020304050102";
        const auto keyPair = sirius::crypto::KeyPair::FromPrivate(sirius::crypto::PrivateKey::FromString( clientPrivateKey ));
        const std::string remoteRpcAddress = "192.168.0.101";
        const int remoteRpcPort = 5510;
        const std::string incomingAddress = "192.168.0.100";
        const int incomingPort = 5550;
        const int incomingRpcPort = 5510;
        const std::filesystem::path workFolder = std::filesystem::path(getenv("HOME")) / "111" / "client_work_folder";

        RpcReplicatorClient rpcReplicatorClient(keyPair, remoteRpcAddress, remoteRpcPort, incomingAddress, incomingPort, incomingRpcPort, workFolder, "client1");
        rpcReplicatorClient.createClientFiles(10 * 1024*1024);

        const std::array<uint8_t,32> drivePubKey{0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0, 0,1};
        const std::array<uint8_t,32> channelKey{0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0, 0,123};

        auto addDriveCallback = [&rpcReplicatorClient, &workFolder, channelKey](const auto& drivePubKey) {

            ActionList actionList;
            actionList.push_back( Action::newFolder( "fff1" ) );
            actionList.push_back( Action::newFolder( "fff1/ffff1" ) );
            actionList.push_back( Action::upload( workFolder / "client_files" / "a.txt", "a.txt" ) );
            actionList.push_back( Action::upload( workFolder / "client_files" / "a.txt", "a2.txt" ) );
            actionList.push_back( Action::upload( workFolder / "client_files" / "b.bin", "f1/b1.bin" ) );
            actionList.push_back( Action::upload( workFolder / "client_files" / "b.bin", "f2/b2.bin" ) );
            actionList.push_back( Action::upload( workFolder / "client_files" / "a.txt", "f2/a.txt" ) );

            const uint64_t maxDataSize = 10;

            auto endDriveModificationCallback = [&rpcReplicatorClient, drivePubKey, channelKey, workFolder]() {
                std::cout << "Client. endDriveModificationCallback." << std::endl;

                const size_t prepaidDownloadSize = 10 * 1024*1024;
                rpcReplicatorClient.openDownloadChannel(channelKey, prepaidDownloadSize, drivePubKey, {rpcReplicatorClient.getPubKey()});

                auto downloadDataCallback = [](download_status::code code,
                                               const InfoHash& infoHash,
                                               const std::filesystem::path filePath,
                                               size_t downloaded,
                                               size_t fileSize,
                                               const std::string& errorText){
                    if ( code == download_status::download_complete )
                    {
                        std::cout << "Client. downloadDataCallback - COMPLETE: " << filePath.string() << " downloaded: " << downloaded <<
                                  " fileSize: " << fileSize << " errorText: " << errorText <<std::endl;
                    }
                    else if ( code == download_status::failed )
                    {
                        std::cout << "Client. downloadDataCallback - FAILED." << " errorText: " << errorText <<std::endl;
                    }
                };

                auto downloadFsTreeCallback = [&rpcReplicatorClient, workFolder, downloadDataCallback](const FsTree& fsTree, download_status::code code){
                    if ( code == download_status::download_complete )
                    {
                        std::cout << "Client. downloadFsTreeCallback - COMPLETE." << std::endl;
                        rpcReplicatorClient.downloadData(fsTree, workFolder.string() + "/downloads", downloadDataCallback);
                    }
                    else if ( code == download_status::failed )
                    {
                        std::cout << "Client. downloadFsTreeCallback - FAILED." << std::endl;
                    }
                };

                rpcReplicatorClient.downloadFsTree(drivePubKey, channelKey, workFolder.string() + "/drives/", downloadFsTreeCallback);
            };

            rpcReplicatorClient.modifyDrive(drivePubKey, actionList, maxDataSize, endDriveModificationCallback);
        };

        rpcReplicatorClient.addDrive(drivePubKey, 100*1024*1024, addDriveCallback);
        rpcReplicatorClient.sync();
    });
    client.detach();

    char a;
    int ret = std::scanf("%c\n", &a);
    (void) ret; // ignore

    return 0;
}
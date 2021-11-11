//#include <drive/RpcReplicatorClient.h>
//#include <drive/ExtensionEmulator.h>
//#include "TestEnvironment.h"
//#include "utils.h"
//
//#include "types.h"
//#include "drive/Session.h"
//#include "drive/ClientSession.h"
//#include "drive/Replicator.h"
//#include "drive/FlatDrive.h"
//#include "drive/FsTree.h"
//#include "drive/Utils.h"
//#include "crypto/Signer.h"
//
//#ifdef __APPLE__
//#pragma mark --main()--
//#endif
//namespace sirius::drive::test {
//
//    class ApprovalReceivedRootCalculatedTestEnvironment : public TestEnvironment {
//    public:
//        ApprovalReceivedRootCalculatedTestEnvironment(
//                int numberOfReplicators,
//                const std::string &ipAddr0,
//                int port0,
//                const std::string &rootFolder0,
//                const std::string &sandboxRootFolder0,
//                bool useTcpSocket,
//                int modifyApprovalDelay,
//                int downloadApprovalDelay,
//                bool startReplicator = true)
//                : TestEnvironment(
//                        numberOfReplicators,
//                        ipAddr0,
//                        port0,
//                        rootFolder0,
//                        sandboxRootFolder0,
//                        useTcpSocket,
//                        modifyApprovalDelay,
//                        downloadApprovalDelay,
//                        startReplicator) {}
//
//                        void modifyApprovalTransactionIsReady(Replicator &replicator, ApprovalTransactionInfo &&transactionInfo) override {
//            transactionInfo.m_opinions.pop_back();
//            TestEnvironment::modifyApprovalTransactionIsReady(replicator, std::move(transactionInfo));
//        }
//    };
//
//    void approvalReceivedRootCalculated() {
//        fs::remove_all(ROOT_FOLDER);
//
//        auto startTime = std::clock();
//
//        gClientFolder = createClientFiles(BIG_FILE_SIZE);
//        gClientSession = createClientSession(std::move(clientKeyPair),
//                                             CLIENT_ADDRESS ":5550",
//                                             clientSessionErrorHandler,
//                                             USE_TCP,
//                                             "client");
//        _LOG("");
//
//        fs::path clientFolder = gClientFolder / "client_files";
//        ApprovalReceivedRootCalculatedTestEnvironment env(
//                NUMBER_OF_REPLICATORS, REPLICATOR_ADDRESS, PORT, DRIVE_ROOT_FOLDER,
//                SANDBOX_ROOT_FOLDER, USE_TCP, 10 * 1000, 10 * 1000);
//
//        EXLOG("\n# Client started: 1-st upload");
//        auto actionList = createActionList();
//        clientModifyDrive(actionList, env.m_addrList, modifyTransactionHash1);
//
//        env.addDrive(DRIVE_PUB_KEY, 100 * 1024 * 1024);
//        env.modifyDrive(DRIVE_PUB_KEY, {clientModifyHash, modifyTransactionHash1, BIG_FILE_SIZE + 1024, env.m_addrList,
//                                        clientKeyPair.publicKey()});
//
//        _LOG("\ntotal time: " << float(std::clock() - startTime) / CLOCKS_PER_SEC);
//        env.waitModificationEnd();
//    }
//}
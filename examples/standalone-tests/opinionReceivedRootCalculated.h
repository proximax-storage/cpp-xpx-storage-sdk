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
//using namespace sirius::drive::test;
//
//namespace sirius::drive::test {
//    class OpinionReceivedRootCalculatedTestEnvironment : public TestEnvironment {
//    public:
//        OpinionReceivedRootCalculatedTestEnvironment(
//                int numberOfReplicators,
//                const std::string &ipAddr0,
//                int port0,
//                const std::string &rootFolder0,
//                const std::string &sandboxRootFolder0,
//                bool useTcpSocket,
//                int modifyApprovalDelay,
//                int downloadApprovalDelay,
//                int minimalDownloadSpeed,
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
//                        startReplicator)
//                        {
//            lt::settings_pack pack;
//            for (uint i = 0; i < m_replicators.size(); i++) {
//                pack.set_int(lt::settings_pack::download_rate_limit, (i + 1u) * minimalDownloadSpeed);
//                m_replicators[i]->setSessionSettings(pack, true);
//            }
//                        }
//
//                        void modifyApprovalTransactionIsReady(Replicator &replicator, ApprovalTransactionInfo &&transactionInfo) override {
//            EXLOG( "modifyApprovalTransactionIsReady: " << replicator.dbgReplicatorName() );
//            const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
//
//            for( const auto& opinion: transactionInfo.m_opinions )
//            {
//                std::cout << " key:" << int(opinion.m_replicatorKey[0]) << " ";
//                for( size_t i=0; i<opinion.m_uploadReplicatorKeys.size(); i+=32  )
//                {
//                    std::cout << int(opinion.m_uploadReplicatorKeys[i]) << ":" << opinion.m_replicatorUploadBytes[i/32] << " ";
//                }
//                std::cout << "client:" <<opinion.m_clientUploadBytes << std::endl;
//            }
//
//            if (replicator.keyPair().publicKey() == m_replicators.back()->keyPair().publicKey())
//            {
//                m_approvalTransactionInfo = { std::move(transactionInfo) };
//
//                for (const auto& r: m_replicators) {
//                    std::thread( [r, this] {
//                        r->onApprovalTransactionHasBeenPublished(*m_approvalTransactionInfo);
//                    }).detach();
//                }
//            }
//        }
//    };
//
//    void opinionReceivedRootCalculatedTest() {
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
//
//        OpinionReceivedRootCalculatedTestEnvironment env(
//                NUMBER_OF_REPLICATORS, REPLICATOR_ADDRESS, PORT, DRIVE_ROOT_FOLDER,
//                SANDBOX_ROOT_FOLDER, USE_TCP, 1, 1, 1024 * 1024);
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
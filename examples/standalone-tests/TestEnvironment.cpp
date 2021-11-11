//#include "TestEnvironment.h"
//
//namespace sirius::drive::test {
//    void TestEnvironment::addDrive(const Key &driveKey, uint64_t driveSize) {
//
//    }
//
//    void TestEnvironment::modifyDrive(const Key &driveKey, const ModifyRequest &request) {
//
//    }
//
//    void TestEnvironment::rootHashIsCalculated(Replicator &replicator,
//                                               const sirius::Key &driveKey,
//                                               const sirius::drive::InfoHash &modifyTransactionHash,
//                                               const sirius::drive::InfoHash &sandboxRootHash) {
//        EXLOG("rootHashIsCalculated: " << replicator.dbgReplicatorName());
//    }
//
//    void TestEnvironment::modifyTransactionIsCanceled(Replicator &replicator, const sirius::Key &driveKey,
//                                                      const sirius::drive::InfoHash &modifyTransactionHash,
//                                                      const std::string &reason, int errorCode) {}
//
//    void TestEnvironment::modifyApprovalTransactionIsReady(Replicator &replicator,
//                                                           ApprovalTransactionInfo &&transactionInfo) {
//
//    }
//
//    void TestEnvironment::singleModifyApprovalTransactionIsReady(Replicator &replicator,
//                                                                 ApprovalTransactionInfo &&transactionInfo) {
//
//    }
//
//    void TestEnvironment::driveModificationIsCompleted(Replicator &replicator,
//                                                       const sirius::Key &driveKey,
//                                                       const sirius::drive::InfoHash &modifyTransactionHash,
//                                                       const sirius::drive::InfoHash &rootHash) {
//
//    }
//
//    void TestEnvironment::waitModificationEnd() {
//
//    }
//
//    TestEnvironment::TestEnvironment(int numberOfReplicators, std::string ipAddr0, int port0, std::string rootFolder0,
//                                     std::string sandboxRootFolder0, bool useTcpSocket, int modifyApprovalDelay,
//                                     int downloadApprovalDelay, bool startReplicator) {
//
//    }
//
//    void TestEnvironment::willBeTerminated(Replicator &replicator) {}
//
//    void TestEnvironment::downloadApprovalTransactionIsReady(Replicator &replicator,
//                                                             const DownloadApprovalTransactionInfo &info) {
//    }
//
//
//}

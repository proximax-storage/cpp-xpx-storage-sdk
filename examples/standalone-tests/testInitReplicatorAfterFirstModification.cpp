#include <drive/ExtensionEmulator.h>
#include "TestEnvironment.h"
#include "utils.h"
#include <set>

#include "types.h"
#include "drive/Session.h"
#include "drive/Replicator.h"
#include "drive/FlatDrive.h"
#include "drive/Utils.h"
#include "gtest/gtest.h"

using namespace sirius::drive::test;

namespace sirius::drive::test {

    /// change this macro for your test
#define TEST_NAME InitReplicatorAfterFirstModification

#define ENVIRONMENT_CLASS JOIN(TEST_NAME, TestEnvironment)

class ENVIRONMENT_CLASS : public TestEnvironment
{
public:
    ENVIRONMENT_CLASS(
            int numberOfReplicators,
            const std::string &ipAddr0,
            int port0,
            const std::string &rootFolder0,
            const std::string &sandboxRootFolder0,
            bool useTcpSocket,
            int modifyApprovalDelay,
            int downloadApprovalDelay,
            int startReplicator = -1)
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
};

TEST(ModificationTest, TEST_NAME) {
    fs::remove_all(ROOT_FOLDER);

    lt::settings_pack pack;
    TestClient client(pack);

    EXLOG("");

    ENVIRONMENT_CLASS env(
            NUMBER_OF_REPLICATORS, REPLICATOR_ADDRESS, PORT, DRIVE_ROOT_FOLDER,
            SANDBOX_ROOT_FOLDER, USE_TCP, 10000, 10000, NUMBER_OF_REPLICATORS - 1);

    EXLOG("\n# Client started: 1-st upload");
    client.modifyDrive(createActionList(CLIENT_WORK_FOLDER), env.m_addrList);
    client.modifyDrive(createActionList_2(CLIENT_WORK_FOLDER), env.m_addrList);

    env.addDrive(DRIVE_PUB_KEY, 100 * 1024 * 1024);
    env.modifyDrive(DRIVE_PUB_KEY, {client.m_actionListHashes[0],
                                    client.m_modificationTransactionHashes[0],
                                    BIG_FILE_SIZE + 1024,
                                    env.m_addrList,
                                    client.m_clientKeyPair.publicKey()});

    env.waitModificationEnd(client.m_modificationTransactionHashes[0], NUMBER_OF_REPLICATORS - 1);

    env.startReplicator(NUMBER_OF_REPLICATORS,
                        REPLICATOR_ADDRESS, PORT, DRIVE_ROOT_FOLDER,
                        SANDBOX_ROOT_FOLDER, USE_TCP, 10000, 10000);

    env.waitModificationEnd(client.m_modificationTransactionHashes[0], NUMBER_OF_REPLICATORS);
}

#undef TEST_NAME
}

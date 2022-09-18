#include "TestEnvironment.h"
#include "utils.h"

#include "types.h"

#include "drive/Utils.h"

using namespace sirius::drive::test;

namespace sirius::drive::test {

/// change this macro for your test
#define TEST_NAME TwoSameModifications

#define ENVIRONMENT_CLASS JOIN(TEST_NAME, TestEnvironment)

class ENVIRONMENT_CLASS
        : public TestEnvironment {
public:
    ENVIRONMENT_CLASS(
            int numberOfReplicators,
            const std::string& ipAddr0,
            int port0,
            const std::string& rootFolder0,
            const std::string& sandboxRootFolder0,
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
            startReplicator) {}
};

TEST(ModificationTest, TEST_NAME) {
    fs::remove_all(ROOT_FOLDER);


    ENVIRONMENT_CLASS env(
            NUMBER_OF_REPLICATORS, REPLICATOR_ADDRESS, PORT, DRIVE_ROOT_FOLDER,
            SANDBOX_ROOT_FOLDER, USE_TCP, 1, 1, 1024 * 1024);

    lt::settings_pack pack;
    pack.set_int(lt::settings_pack::upload_rate_limit, 0);
    endpoint_list bootstraps;
    for ( const auto& b: env.m_bootstraps )
    {
        bootstraps.push_back( b.m_endpoint );
    }
    TestClient client(bootstraps, pack);

    client.modifyDrive(createActionList(CLIENT_WORK_FOLDER / toString(DRIVE_PUB_KEY)), {}, DRIVE_PUB_KEY);
    client.modifyDrive(createActionList(CLIENT_WORK_FOLDER / toString(DRIVE_PUB_KEY_2)), {}, DRIVE_PUB_KEY_2);
    ASSERT_NE(client.m_actionListHashes[0], client.m_actionListHashes[1]);

    env.addDrive(DRIVE_PUB_KEY, client.m_clientKeyPair.publicKey(), 1024 * 1024 * 1024);
    env.addDrive(DRIVE_PUB_KEY_2, client.m_clientKeyPair.publicKey(), 1024 * 1024 * 1024);

    env.modifyDrive(DRIVE_PUB_KEY, {client.m_actionListHashes[0],
                                    client.m_modificationTransactionHashes[0],
                                    BIG_FILE_SIZE + 1024 * 1024,
                                    env.m_addrList });

    env.waitModificationEnd(client.m_modificationTransactionHashes[0], NUMBER_OF_REPLICATORS);

    env.modifyDrive(DRIVE_PUB_KEY_2, {client.m_actionListHashes[1],
                                    client.m_modificationTransactionHashes[1],
                                    BIG_FILE_SIZE + 1024 * 1024,
                                    env.m_addrList });

    env.waitModificationEnd(client.m_modificationTransactionHashes[1], NUMBER_OF_REPLICATORS);
}

#undef TEST_NAME
}

#include "TestEnvironment.h"
#include "utils.h"
#include "gtest/gtest.h"

#include "types.h"

#include "drive/Utils.h"

using namespace sirius::drive::test;

namespace sirius::drive::test
{

/// change this macro for your test
#define TEST_NAME RemoveUnusedDrives

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
                int downloadRateLimit,
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
        {
            lt::settings_pack pack;
            pack.set_int(lt::settings_pack::download_rate_limit, downloadRateLimit);
            for (auto &replicator: m_replicators)
            {
                replicator->setSessionSettings(pack, true);
            }
        }
    };

    TEST(ModificationTest, TEST_NAME)
    {
        fs::remove_all(ROOT_FOLDER);

        ENVIRONMENT_CLASS env(
                1, REPLICATOR_ADDRESS, PORT, DRIVE_ROOT_FOLDER,
                SANDBOX_ROOT_FOLDER, USE_TCP, 1, 1, 2 * 1024 * 1024);

        auto driveToRemove = randomByteArray<Key>();
        auto driveNotToRemove = randomByteArray<Key>();

        env.addDrive( driveToRemove, randomByteArray<Key>(), 0, {} );
        env.addDrive( driveNotToRemove, randomByteArray<Key>(), 0, {} );

        env.stopReplicator(1);

        sleep(5);

        env.m_drives.erase(driveToRemove);

        env.startReplicator(1,
                            REPLICATOR_ADDRESS, PORT, DRIVE_ROOT_FOLDER,
                            SANDBOX_ROOT_FOLDER, USE_TCP, 10000, 10000);

        sleep(5);

        const auto& rootFolder = env.m_rootFolders[0];
        const auto& sandboxFolder = env.m_sandboxFolders[0];
        EXPECT_TRUE( !fs::exists( fs::path(rootFolder) / toString(driveToRemove.array())) );
        EXPECT_TRUE( !fs::exists( fs::path(sandboxFolder) / toString(driveToRemove.array())) );
        EXPECT_TRUE( fs::exists( fs::path(rootFolder) / toString(driveNotToRemove.array())) );
        EXPECT_TRUE( fs::exists( fs::path(sandboxFolder) / toString(driveNotToRemove.array())) );
    }

#undef TEST_NAME
}



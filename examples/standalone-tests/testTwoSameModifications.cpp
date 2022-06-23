#include "TestEnvironment.h"
#include "utils.h"

#include "types.h"

#include "drive/Utils.h"

using namespace sirius::drive::test;

namespace sirius::drive::test
{

    /// change this macro for your test
#define TEST_NAME TwoSameModifications

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
            for ( const auto& r: m_replicators )
            {
                if ( r )
                {
                    lt::settings_pack pack;
                    pack.set_int(lt::settings_pack::download_rate_limit, 1024 * 1024);
                    for (auto replicator: m_replicators)
                    {
                        if (replicator)
                        {
//                            replicator->setSessionSettings(pack, true);
                        }
                    }
                }
            }
        }
    };

    TEST(ModificationTest, TEST_NAME)
    {
        fs::remove_all(ROOT_FOLDER);

        TestClient client( {}, {});

        client.modifyDrive( createActionList(CLIENT_WORK_FOLDER), {}, DRIVE_PUB_KEY );
        client.modifyDrive( createActionList(CLIENT_WORK_FOLDER), {}, DRIVE_PUB_KEY_2 );
        ASSERT_NE( client.m_actionListHashes[0], client.m_actionListHashes[1] );
    }

#undef TEST_NAME
}

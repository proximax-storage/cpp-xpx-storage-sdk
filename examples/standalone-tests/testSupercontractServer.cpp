#include <numeric>
#include "TestEnvironment.h"
#include "utils.h"

#include "types.h"

#include "drive/Utils.h"

using namespace sirius::drive::test;

namespace sirius::drive::test
{

/// change this macro for your test
#define TEST_NAME SupercontractServer

#define ENVIRONMENT_CLASS JOIN(TEST_NAME, TestEnvironment)

class ENVIRONMENT_CLASS
        : public TestEnvironment
{
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
            int downloadRateLimit,
            int startReplicator = -1 )
            : TestEnvironment(
            numberOfReplicators,
            ipAddr0,
            port0,
            rootFolder0,
            sandboxRootFolder0,
            useTcpSocket,
            modifyApprovalDelay,
            downloadApprovalDelay,
            startReplicator,
            true )
    {}
};

TEST( ModificationTest, TEST_NAME )
{
    fs::remove_all( ROOT_FOLDER );

    EXLOG( "" );

    ENVIRONMENT_CLASS env(
            1, REPLICATOR_ADDRESS, PORT, DRIVE_ROOT_FOLDER,
            SANDBOX_ROOT_FOLDER, USE_TCP, 1, 1, 1024 * 1024 );

    lt::settings_pack pack;
    endpoint_list bootstraps;
    for ( const auto& b: env.m_bootstraps )
    {
        bootstraps.push_back( b.m_endpoint );
    }

    Key driveKey{{1}};
    env.addDrive( driveKey, Key(), 100 * 1024 * 1024 );

    std::this_thread::sleep_for( std::chrono::minutes( 10 ));
    auto replicator = env.getReplicator( env.m_addrList.front());
    replicator->dbgPrintDriveStatus( driveKey );
}

#undef TEST_NAME
}

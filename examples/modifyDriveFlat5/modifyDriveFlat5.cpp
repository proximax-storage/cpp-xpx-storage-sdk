
#include "TestEnvironment.h"
#include "utils.h"
#include <set>
#include <numeric>

#include "types.h"
#include "../../src/drive/Session.h"
#include "drive/Replicator.h"
#include "drive/FlatDrive.h"
#include "drive/Utils.h"

using namespace sirius::drive::test;

class Environment : public TestEnvironment
{
public:
   Environment( int numberOfReplicators,
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

    void modifyApprovalTransactionIsReady(Replicator &replicator, const ApprovalTransactionInfo& transactionInfo) override
    {
        TestEnvironment::modifyApprovalTransactionIsReady(replicator, ApprovalTransactionInfo(transactionInfo));
    }

    void singleModifyApprovalTransactionIsReady(Replicator &replicator,
                                                const ApprovalTransactionInfo& transactionInfo) override
    {
        TestEnvironment::singleModifyApprovalTransactionIsReady(replicator, std::move(transactionInfo));
    };
    
    void driveIsRemoved(  Replicator& replicator, const sirius::Key& driveKey ) override
    {
        m_barrier.set_value();
    }
    
    std::future<void> getBarrierFuture()
    {
        std::promise<void> barrier;
        m_barrier = std::move( barrier );
        return m_barrier.get_future();
    }
    
    std::promise<void> m_barrier;
};

int main()
{
    fs::remove_all(ROOT_FOLDER);

    auto startTime = std::clock();

    EXLOG("");
    gBreakOnError = false;
    
    // Prepare Environment
    Environment env( 5, //NUMBER_OF_REPLICATORS
                    REPLICATOR_ADDRESS, PORT, DRIVE_ROOT_FOLDER, SANDBOX_ROOT_FOLDER, USE_TCP, 100, 100 );

    // Prepare 'bootstrap list'
    lt::settings_pack pack;
    endpoint_list bootstraps;
    for ( const auto& b: env.m_bootstraps )
    {
         bootstraps.push_back( b.m_endpoint );
    }
    
    //
    // Create 'client'
    //
    TestClient client(bootstraps, pack);

    //
    // Add drive to replicators
    //
    env.addDrive( DRIVE_PUB_KEY, client.m_clientKeyPair.publicKey(), 100*1024*1024 );
    auto barrierFuture = env.getBarrierFuture();
    env.m_replicators[4]->asyncRemoveDrive( DRIVE_PUB_KEY );

    env.m_replicators[0]->asyncReplicatorRemoved( DRIVE_PUB_KEY, env.m_replicators[4]->replicatorKey() );
    env.m_replicators[1]->asyncReplicatorRemoved( DRIVE_PUB_KEY, env.m_replicators[4]->replicatorKey() );
    env.m_replicators[2]->asyncReplicatorRemoved( DRIVE_PUB_KEY, env.m_replicators[4]->replicatorKey() );
    env.m_replicators[3]->asyncReplicatorRemoved( DRIVE_PUB_KEY, env.m_replicators[4]->replicatorKey() );
    barrierFuture.wait();

    //
    // Start 1-st Modification
    //
    EXLOG("\n# Client started: 1-st upload");
    auto actionList = createActionList(CLIENT_WORK_FOLDER);
    client.modifyDrive( actionList, env.m_addrList, DRIVE_PUB_KEY );

    ModificationRequest modificationRequest1 {  client.m_actionListHashes.back(),
                                    client.m_modificationTransactionHashes.back(),
                                    BIG_FILE_SIZE + 1024,
                                    env.m_addrList
                                    };
    env.modifyDrive( DRIVE_PUB_KEY, modificationRequest1 );
                    
    // Wait Modification End
    env.waitModificationEnd(client.m_modificationTransactionHashes.back(), NUMBER_OF_REPLICATORS-1);

    //
    // Download files
    //
    auto downloadChannel = randomByteArray<sirius::Key>();
    auto downloadRequest = DownloadRequest {
        downloadChannel,
        BIG_FILE_SIZE + 1024 * 1024,
        env.m_addrList,
        {client.m_clientKeyPair.publicKey()}
    };

    env.addDownloadChannelInfo( DRIVE_PUB_KEY, downloadRequest );

    client.downloadFromDrive( env.m_lastApprovedModification->m_rootHash, downloadChannel, env.m_addrList );

    client.waitForDownloadComplete( env.m_lastApprovedModification->m_rootHash );

    //
    // Add Drive to 5-th replicator
    //
    env.m_replicators[4]->asyncAddDrive( DRIVE_PUB_KEY, env.drive->second );
    env.m_replicators[4]->asyncAddDownloadChannelInfo( DRIVE_PUB_KEY, DownloadRequest(downloadRequest), true );
    env.m_replicators[4]->asyncModify( DRIVE_PUB_KEY, modificationRequest1 );
    env.m_replicators[4]->asyncApprovalTransactionHasBeenPublished(*env.m_lastApprovedModification);

    env.m_replicators[0]->asyncReplicatorAdded( DRIVE_PUB_KEY, env.m_replicators[4]->replicatorKey() );
    env.m_replicators[1]->asyncReplicatorAdded( DRIVE_PUB_KEY, env.m_replicators[4]->replicatorKey() );
    env.m_replicators[2]->asyncReplicatorAdded( DRIVE_PUB_KEY, env.m_replicators[4]->replicatorKey() );
    env.m_replicators[3]->asyncReplicatorAdded( DRIVE_PUB_KEY, env.m_replicators[4]->replicatorKey() );

    // Start 2-d Modification
    EXLOG("\n# Client started: 2-st upload");
    actionList = createActionList_2( CLIENT_WORK_FOLDER );
    client.modifyDrive( actionList, env.m_addrList, DRIVE_PUB_KEY );

    env.modifyDrive( DRIVE_PUB_KEY, { client.m_actionListHashes[1],
                                      client.m_modificationTransactionHashes[1],
                                      BIG_FILE_SIZE + 1024,
                                      env.m_addrList });

    // Wait Modification End
    env.waitModificationEnd( client.m_modificationTransactionHashes[1], NUMBER_OF_REPLICATORS );

    EXLOG("\n\ntotal time: " << float(std::clock() - startTime) / CLOCKS_PER_SEC << "\n\n" );
    return 0;
}

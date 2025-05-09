#include "TestEnvironment.h"
#include "utils.h"
#include <set>
#include <numeric>

#include "types.h"

#include "drive/Replicator.h"
#include "drive/FlatDrive.h"
#include "drive/Utils.h"
#include "gtest/gtest.h"

using namespace sirius::drive::test;

namespace sirius::drive::test
{

    /// change this macro for your test
#define TEST_NAME DownloadSmallChannel

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
        {
            lt::settings_pack pack;
            pack.set_int(lt::settings_pack::download_rate_limit, 1024 * 1024);
            pack.set_int(lt::settings_pack::upload_rate_limit, 1024 * 1024);
            for (auto replicator: m_replicators)
            {
                if (replicator)
                {
                    replicator->setSessionSettings(pack, true);
                }
            }
        }
    };

    TEST(ModificationTest, TEST_NAME)
    {
        fs::remove_all(ROOT_FOLDER);

        auto startTime = std::clock();

        EXLOG("");

        ENVIRONMENT_CLASS env(
                NUMBER_OF_REPLICATORS, REPLICATOR_ADDRESS, PORT, DRIVE_ROOT_FOLDER,
                SANDBOX_ROOT_FOLDER, USE_TCP, 10000, 10000);

        lt::settings_pack pack;
        pack.set_int(lt::settings_pack::download_rate_limit, 1024 * 1024);
        pack.set_int(lt::settings_pack::upload_rate_limit, 1024 * 1024);
        endpoint_list bootstraps;
        for ( const auto& b: env.m_bootstraps )
        {
            bootstraps.push_back( b.m_endpoint );
        }
        TestClient client(bootstraps, pack);

        EXLOG("\n# Client started: 1-st upload");
        auto actionList = createActionList(CLIENT_WORK_FOLDER);
        client.modifyDrive(actionList, env.m_addrList, DRIVE_PUB_KEY);

        env.addDrive(DRIVE_PUB_KEY, client.m_clientKeyPair.publicKey(), 100 * 1024 * 1024);
        env.modifyDrive(DRIVE_PUB_KEY, {client.m_actionListHashes.back(),
                                        client.m_modificationTransactionHashes.back(),
                                        BIG_FILE_SIZE + 1024 * 1024,
                                        env.m_addrList });

        EXLOG("\ntotal time: " << float(std::clock() - startTime) / CLOCKS_PER_SEC);
        env.waitModificationEnd(client.m_modificationTransactionHashes.back(), NUMBER_OF_REPLICATORS);

        client.removeModifyTorrents();

        auto downloadChannel = randomByteArray<Key>();

        env.downloadFromDrive(DRIVE_PUB_KEY, DownloadRequest{
                downloadChannel,
                BIG_FILE_SIZE / 2,
                env.m_addrList,
                {client.m_clientKeyPair.publicKey()}
        });

        client.downloadFromDrive(env.m_drives[DRIVE_PUB_KEY].m_lastApprovedModification->m_rootHash, downloadChannel, env.m_addrList);

        client.waitForDownloadComplete(env.m_drives[DRIVE_PUB_KEY].m_lastApprovedModification->m_rootHash);

        auto files = client.getFsTreeFiles();

        for (const auto &file: files)
        {
            client.downloadFromDrive(file, downloadChannel, env.m_addrList);
        }

        std::this_thread::sleep_for(std::chrono::seconds(20));

        int downloaded = 0;
        for (auto &file: files)
        {
            if (client.m_downloadCompleted[file])
            {
                downloaded++;
            }
        }
        EXLOG( "Download " << downloaded );
        ASSERT_LT(downloaded, files.size());
    }

#undef TEST_NAME
}

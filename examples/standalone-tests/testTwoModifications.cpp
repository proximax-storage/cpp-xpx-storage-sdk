#include <numeric>
#include "TestEnvironment.h"
#include "utils.h"

#include "types.h"

#include "drive/Utils.h"

using namespace sirius::drive::test;

namespace sirius::drive::test
{

    /// change this macro for your test
#define TEST_NAME TwoModifications

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

        std::optional<Hash256> m_forbiddenTransaction;

        virtual void
        modifyApprovalTransactionIsReady(Replicator &replicator, const ApprovalTransactionInfo &transactionInfo) override
        {
            auto transaction = transactionInfo;
            if (transaction.m_opinions.size() == m_replicators.size())
            {
                transaction.m_opinions.pop_back();
            }
            TestEnvironment::modifyApprovalTransactionIsReady(replicator, transaction);
        }

        virtual void singleModifyApprovalTransactionIsReady(Replicator &replicator,
                                                            const ApprovalTransactionInfo &transactionInfo) override
        {
            if (m_forbiddenTransaction != transactionInfo.m_modifyTransactionHash)
            {
                TestEnvironment::singleModifyApprovalTransactionIsReady(replicator,
                                                                        ApprovalTransactionInfo(transactionInfo));
            }
        };
    };

    TEST(ModificationTest, TEST_NAME)
    {
        fs::remove_all(ROOT_FOLDER);

        EXLOG("");

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

        EXLOG("\n# Client started: 1-st upload");
        client.modifyDrive(createActionList(CLIENT_WORK_FOLDER), env.m_addrList, DRIVE_PUB_KEY);
        client.modifyDrive(createActionList_2(CLIENT_WORK_FOLDER), env.m_addrList, DRIVE_PUB_KEY);

        env.m_forbiddenTransaction.emplace(client.m_modificationTransactionHashes[0]);

        env.addDrive(DRIVE_PUB_KEY, client.m_clientKeyPair.publicKey(), 100 * 1024 * 1024);
        env.modifyDrive(DRIVE_PUB_KEY, {client.m_actionListHashes[0],
                                        client.m_modificationTransactionHashes[0],
                                        BIG_FILE_SIZE + 1024 * 1024,
                                        env.m_addrList });

        EXLOG("Required modification " << toString(client.m_modificationTransactionHashes[0]));

        env.modifyDrive(DRIVE_PUB_KEY, {client.m_actionListHashes[1],
                                        client.m_modificationTransactionHashes[1],
                                        BIG_FILE_SIZE + 1024 * 1024,
                                        env.m_addrList });

        EXLOG("Required modification " << toString(client.m_modificationTransactionHashes[1]));

        env.waitModificationEnd(client.m_modificationTransactionHashes[1], NUMBER_OF_REPLICATORS);
    }

#undef TEST_NAME
}

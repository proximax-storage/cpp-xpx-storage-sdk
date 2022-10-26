#include <numeric>
#include <drive/Replicator.h>

#include "types.h"

#include "drive/Utils.h"

using namespace sirius::drive;

class MockEventHandler
        : public ReplicatorEventHandler {
public:
    void modifyApprovalTransactionIsReady( Replicator& replicator,
                                           const ApprovalTransactionInfo& transactionInfo ) override {

    }

    void singleModifyApprovalTransactionIsReady( Replicator& replicator,
                                                 const ApprovalTransactionInfo& transactionInfo ) override {

    }

    void
    downloadApprovalTransactionIsReady( Replicator& replicator, const DownloadApprovalTransactionInfo& info ) override {

    }

    void opinionHasBeenReceived( Replicator& replicator, const ApprovalTransactionInfo& info ) override {

    }

    void
    downloadOpinionHasBeenReceived( Replicator& replicator, const DownloadApprovalTransactionInfo& info ) override {

    }
};

int main() {
    std::string privateKey = "1100000000010203040501020304050102030405010203040501020304050102";

    auto keyPair = sirius::crypto::KeyPair::FromPrivate(
            sirius::crypto::PrivateKey::FromString( privateKey ));

    MockEventHandler eventHandler;

    auto drivePath = fs::path( getenv( "HOME" )) / "111" / "Drive01";
    auto sandboxPath = fs::path( getenv( "HOME" )) / "111" / "Sandbox01";

    fs::remove_all( drivePath );
    fs::remove_all( sandboxPath );

    auto replicator = createDefaultReplicator(
            keyPair,
            "192.168.2.200",
            std::to_string( 5555 ),
            fs::path( drivePath ),
            fs::path( sandboxPath ),
            {},
            false,
            eventHandler,
            nullptr,
            "replicator" );

    replicator->start();
    replicator->enableSupercontractServer( "127.0.0.1:5551" );

    for ( unsigned char i = 0U; i < 100U; i++ ) {
        sirius::Key driveKey{{i}};
        AddDriveRequest request{1024ULL * 1024ULL * 1024ULL, 0, {}, {keyPair.publicKey()}, sirius::Key(), {}, {}};
        replicator->asyncAddDrive( driveKey, request );
    }

    std::cout << "Press Enter To Stop" << std::endl;
    std::cin.get();

    replicator.reset();
    fs::remove_all( drivePath );
    fs::remove_all( sandboxPath );

    return 0;
}

#include "emulator/RpcReplicator.h"
#include <array>
#include <condition_variable>


//
// This example shows interaction between 'client' and 'replicator'.
//

#define REPLICATOR_NAME                 "replicator1"
#define REPLICATOR_PRIVATE_KEY          "1000000000010203040501020304050102030405010203040501020304050102"
#define REPLICATOR_ADDRESS              "192.168.0.102"
#define REPLICATOR_PORT                 "5550"
#define RPC_PORT                        5510
#define REPLICATOR_ROOT_FOLDER          (std::string(getenv("HOME"))+"/111/replicator1/replicator_root")
#define REPLICATOR_SANDBOX_ROOT_FOLDER  (std::string(getenv("HOME"))+"/111/replicator1/sandbox_root")
#define EMULATOR_RPC_ADDRESS            "192.168.0.101"
#define EMULATOR_RPC_PORT               5510

namespace fs = std::filesystem;

using namespace sirius::emulator;
using namespace sirius::drive;

inline std::mutex gExLogMutex;

int main()
{
    const auto keyPair = sirius::crypto::KeyPair::FromPrivate(sirius::crypto::PrivateKey::FromString( REPLICATOR_PRIVATE_KEY ));
    RpcReplicator replicator( keyPair,
                              REPLICATOR_NAME,
                              REPLICATOR_ADDRESS,
                              REPLICATOR_PORT,
                              REPLICATOR_ROOT_FOLDER,
                              REPLICATOR_SANDBOX_ROOT_FOLDER,
                              RPC_PORT,
                              EMULATOR_RPC_ADDRESS,
                              EMULATOR_RPC_PORT);

    replicator.run();

    return 0;
}

#include "drive/RpcReplicator.h"
#include "drive/Utils.h"

#include "rpc/server.h"

#include <fstream>
#include <filesystem>
#include <future>
#include <array>
#include <condition_variable>
#include "boost/date_time/posix_time/posix_time.hpp"

#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>


//
// This example shows interaction between 'client' and 'replicator'.
//

#define REPLICATOR_PORT "5550"
#define RPC_PORT 5510
#define REPLICATOR_ROOT_FOLDER          (std::string(getenv("HOME"))+"/111/replicator_root")
#define REPLICATOR_SANDBOX_ROOT_FOLDER  (std::string(getenv("HOME"))+"/111/sandbox_root")

namespace fs = std::filesystem;

using namespace sirius::drive;

inline std::mutex gExLogMutex;

int main()
{
    system("pwd\n");
    LOG("Replicator started");
    RpcReplicator replicator( REPLICATOR_PORT,
                              REPLICATOR_ROOT_FOLDER,
                              REPLICATOR_SANDBOX_ROOT_FOLDER,
                              RPC_PORT );

    replicator.runRpcServer();

    return 0;
}

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

// !!!
// CLIENT_IP_ADDR should be changed to proper address according to your network settings (see ifconfig)

#define CLIENT_IP_ADDR "192.168.1.100"
#define REPLICATOR_IP_ADDR "127.0.0.1"
#define REPLICATOR_ROOT_FOLDER          "/Users/alex/111/replicator_root"
#define REPLICATOR_SANDBOX_ROOT_FOLDER  "/Users/alex/111/sandbox_root"
#define DRIVE_PUB_KEY                   "pub_key"

namespace fs = std::filesystem;

using namespace sirius::drive;

inline std::mutex gExLogMutex;

//static std::string now_str();

//#define EXLOG(expr) { \
//        const std::lock_guard<std::mutex> autolock( gExLogMutex ); \
//        std::cout << now_str() << ": " << expr << std::endl << std::flush; \
//    }
//
//#define EXLOG_ERR(expr) { \
//        const std::lock_guard<std::mutex> autolock( gExLogMutex ); \
//        std::cerr << now_str() << ": ERROR: " << expr << std::endl << std::flush; \
//    }

int main()
{
    system("pwd\n");
    LOG("started");
    RpcReplicator replicator( "5550", REPLICATOR_ROOT_FOLDER, REPLICATOR_SANDBOX_ROOT_FOLDER, 5510 );

    replicator.runRpcServer();

    return 0;
}



//static std::string now_str()
//{
//    // Get current time from the clock, using microseconds resolution
//    const boost::posix_time::ptime now =
//        boost::posix_time::microsec_clock::local_time();
//
//    // Get the time offset in current day
//    const boost::posix_time::time_duration td = now.time_of_day();
//
//    //
//    // Extract hours, minutes, seconds and milliseconds.
//    //
//    // Since there is no direct accessor ".milliseconds()",
//    // milliseconds are computed _by difference_ between total milliseconds
//    // (for which there is an accessor), and the hours/minutes/seconds
//    // values previously fetched.
//    //
//    const long hours        = td.hours();
//    const long minutes      = td.minutes();
//    const long seconds      = td.seconds();
//    const long milliseconds = td.total_milliseconds() -
//                              ((hours * 3600 + minutes * 60 + seconds) * 1000);
//
//    //
//    // Format like this:
//    //
//    //      hh:mm:ss.SSS
//    //
//    // e.g. 02:15:40:321
//    //
//    //      ^          ^
//    //      |          |
//    //      123456789*12
//    //      ---------10-     --> 12 chars + \0 --> 13 chars should suffice
//    //
//    //
//    char buf[40];
//    sprintf(buf, "%02ld:%02ld:%02ld.%03ld",
//        hours, minutes, seconds, milliseconds);
//
//    return buf;
//}


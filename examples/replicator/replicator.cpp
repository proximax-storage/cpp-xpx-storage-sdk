#include "drive/Session.h"
#include "drive/Drive.h"
#include "drive/Utils.h"

#include <fstream>
#include <filesystem>
#include <future>
#include <condition_variable>
#include <boost/asio.hpp>
#include "boost/date_time/posix_time/posix_time.hpp"

#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>


// !!!
// CLIENT_IP_ADDR should be changed to proper address according to your network settings (see ifconfig)

#define REPLICATOR_IP_ADDR "127.0.0.1"
#define REPLICATOR_ROOT_FOLDER          (std::string(getenv("HOME")) + "/1111/replicator_root")
#define REPLICATOR_SANDBOX_ROOT_FOLDER  (std::string(getenv("HOME")) + "/1111/sandbox_root")
#define DRIVE_PUB_KEY                   "pub_key"

namespace fs = std::filesystem;

using namespace sirius::drive;

inline std::mutex gExLogMutex;

#define EXLOG(expr) { \
        const std::lock_guard<std::mutex> autolock( gLogMutex ); \
        std::cout << expr << std::endl << std::flush; \
    }


// forward declarations
//
void replicator();

// global variables, which help synchronize client and replicator
//
bool                        isDriveUpdated = false;
std::shared_ptr<InfoHash>   driveRootHash;
std::condition_variable     driveCondVar;
std::mutex                  driveMutex;

// replicatorSessionErrorHandler
void replicatorSessionErrorHandler( libtorrent::alert* alert)
{
    if ( alert->type() == lt::listen_failed_alert::alert_type )
    {
        std::cerr << alert->message() << std::endl << std::flush;
        exit(-1);
    }
}


//
// main
//
int main(int,char**)
{
    EXLOG("started");

    // Start replicator
    std::thread replicatorThread( replicator );

    replicatorThread.join();

    return 0;
}

//
// replicatorDownloadHandler
//
void replicatorDownloadHandler ( modify_status::code code, InfoHash /*resultRootInfoHash*/, std::string error )
{

    if ( code == modify_status::update_completed )
    {
        EXLOG( "@ update_completed: " );

        isDriveUpdated = true;
        driveCondVar.notify_all();
    }
    else if ( code == modify_status::sandbox_root_hash )
    {
        EXLOG( "@ sandbox calculated" );
    }
    else
    {
        EXLOG( "ERROR: " << error );
        exit(-1);
    }
}

//
// replicator
//
void replicator()
{
    boost::system::error_code ec;

    EXLOG( "@ Replicator started" );

    // Create Libtorrent Session
    auto session = createDefaultSession( REPLICATOR_IP_ADDR":5550", replicatorSessionErrorHandler );

    // Start drive
    fs::remove_all( REPLICATOR_ROOT_FOLDER );
    fs::remove_all( REPLICATOR_SANDBOX_ROOT_FOLDER );
    auto drive = createDefaultDrive( session,
                                     REPLICATOR_ROOT_FOLDER,
                                     REPLICATOR_SANDBOX_ROOT_FOLDER,
                                     DRIVE_PUB_KEY,
                                     100*1024*1024, {} );

    boost::asio::io_service io_service;

    // Listening for any new incomming connection
    tcp::acceptor acceptor_server( io_service, tcp::endpoint( tcp::v4(), 9999 ));

    // Creating socket object
    tcp::socket server_socket(io_service);

    // waiting for connection
    acceptor_server.accept(server_socket);

    boost::asio::socket_base::keep_alive option(true);
    server_socket.set_option( option );

    // Send FsTree/root hash
    auto rootHash = drive->rootDriveHash();
    EXLOG( "@ Drive rootHash: " << toString(rootHash) );
    boost::asio::write( server_socket, boost::asio::buffer( rootHash ), ec );
    if (ec) {
        EXLOG( "boost::asio::write(0) error:" << ec.message() );
        exit(-1);
    }

    for(;;)
    {
        EXLOG( "@ Replicator is waiting of client data infoHash" );

        // Read modify hash
        boost::asio::streambuf buf;
        boost::asio::read( server_socket, buf, boost::asio::transfer_exactly(32), ec );
        if (ec) {
            EXLOG( "boost::asio::read error:" << ec.message() );
            exit(-1);
        }

        InfoHash infoHash;
        memcpy( &infoHash[0], boost::asio::buffer_cast<const void*>(buf.data()), 32 );

        EXLOG( "@ Replicator received client data infoHash: " << toString(infoHash) );
        isDriveUpdated = false;
        drive->startModifyDrive( infoHash, replicatorDownloadHandler );

        // wait the end of drive update
        {
            std::unique_lock<std::mutex> lock(driveMutex);
            driveCondVar.wait( lock, [] { return isDriveUpdated; } );
        }

        // Send FsTree/root hash
        rootHash = drive->rootDriveHash();
        EXLOG( "@ Next drive rootHash: " << toString(rootHash) );
        boost::asio::write( server_socket, boost::asio::buffer( rootHash ) );
        if (ec) {
            EXLOG( "boost::asio::write error:" << ec.message() );
            exit(-1);
        }
    }

    EXLOG( "@ Replicator exited" );
}

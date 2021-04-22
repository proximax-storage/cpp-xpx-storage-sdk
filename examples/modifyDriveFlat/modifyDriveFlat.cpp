#include "drive/Session.h"
#include "drive/FlatDrive.h"
#include "drive/FsTree.h"
#include "drive/Utils.h"

#include <fstream>
#include <filesystem>
#include <future>
#include <condition_variable>
#include "boost/date_time/posix_time/posix_time.hpp"

#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>


//
// This example shows interaction between 'client' and 'replicator'.
//

// !!!
// CLIENT_IP_ADDR should be changed to proper address according to your network settings (see ifconfig)

#define CLIENT_IP_ADDR "192.168.1.101"
#define REPLICATOR_IP_ADDR "127.0.0.1"
#define REPLICATOR_ROOT_FOLDER          "/Users/alex/111/replicator_root"
#define REPLICATOR_SANDBOX_ROOT_FOLDER  "/Users/alex/111/sandbox_root"
#define DRIVE_PUB_KEY                   "pub_key"

namespace fs = std::filesystem;

using namespace sirius::drive;

inline std::mutex gExLogMutex;

static std::string now_str();

#define EXLOG(expr) { \
        const std::lock_guard<std::mutex> autolock( gExLogMutex ); \
        std::cout << now_str() << ": " << expr << std::endl << std::flush; \
    }


// forward declarations
//
void replicator( );
void clientDownloadFsTree( InfoHash rootHash, endpoint_list addrList );
void clientUploadFiles( endpoint_list addrList );
void clientModifyDrive( endpoint_list addrList );
fs::path createClientFiles();
fs::path createClientFiles2();

// global variables, which help synchronize client and replicator
//

bool                    isFsTreeReceived = false;
std::condition_variable fsTreeCondVar;
std::mutex              fsTreeMutex;

bool                    isDriveUpdated = false;
std::condition_variable driveUpdateCondVar;
std::mutex              driveUpdateMutex;

std::promise<InfoHash>  rootHashPromise;
std::promise<InfoHash>  rootHashPromise2;
std::promise<InfoHash>  rootHashPromise3;
std::promise<InfoHash>  clientDataPromise;
std::promise<InfoHash>  clientDataPromise2;

// clientSessionErrorHandler
void clientSessionErrorHandler( const lt::alert* alert )
{
    if ( alert->type() == lt::listen_failed_alert::alert_type )
    {
        std::cerr << alert->message() << std::endl << std::flush;
        exit(-1);
    }
}

// replicatorSessionErrorHandler
void replicatorSessionErrorHandler( const lt::alert* alert)
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
    EXLOG("started+");

    // Start replicator
    std::thread replicatorThread( replicator );

    // Make the list of replicator addresses
    //
    endpoint_list replicatorsList;
    boost::asio::ip::address e = boost::asio::ip::address::from_string(CLIENT_IP_ADDR);
    replicatorsList.emplace_back( e, 5551 );

    // Client: read fsTree
    isFsTreeReceived = false;
    clientDownloadFsTree( rootHashPromise.get_future().get(), replicatorsList );

    // Client: upload files
    {
        std::unique_lock<std::mutex> lock(driveUpdateMutex);
        isDriveUpdated = false;
    }
    clientUploadFiles( replicatorsList );

    // Client: read new fsTree
    isFsTreeReceived = false;
    clientDownloadFsTree( rootHashPromise2.get_future().get(), replicatorsList );

    // Client: modify drive
    {
        std::unique_lock<std::mutex> lock(driveUpdateMutex);
        isDriveUpdated = false;
    }
    isFsTreeReceived = false;
    clientModifyDrive( replicatorsList );

    // Client: read new fsTree
    clientDownloadFsTree( rootHashPromise3.get_future().get(), replicatorsList );

    replicatorThread.join();

    return 0;
}

//
// replicatorDownloadHandler
//
void replicatorDownloadHandler ( modify_status::code code, InfoHash /*resultRootInfoHash*/, const std::string& error )
{

    if ( code == modify_status::update_completed )
    {
        EXLOG( "@ update_completed" );

        isDriveUpdated = true;
        driveUpdateCondVar.notify_all();
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
    EXLOG( "@ Replicator started" );

    auto session = createDefaultSession( CLIENT_IP_ADDR":5551", replicatorSessionErrorHandler );

    // start drive
    fs::remove_all( REPLICATOR_ROOT_FOLDER );
    fs::remove_all( REPLICATOR_SANDBOX_ROOT_FOLDER );
    auto drive = createDefaultFlatDrive( session,
                                         REPLICATOR_ROOT_FOLDER,
                                         REPLICATOR_SANDBOX_ROOT_FOLDER,
                                         DRIVE_PUB_KEY,
                                         100*1024*1024, {} );

    // set root drive hash
    rootHashPromise.set_value( drive->rootDriveHash() );

    // wait client data infoHash (1)
    EXLOG( "@ Replicator is waiting of client data infoHash" );
    InfoHash infoHash = clientDataPromise.get_future().get();
    std::cout << "\n";
    EXLOG( "@ Replicator received client data infoHash" );
    
    // start drive update
    drive->startModifyDrive( infoHash, replicatorDownloadHandler );

    // wait the end of drive update
    {
        std::unique_lock<std::mutex> lock(driveUpdateMutex);
        driveUpdateCondVar.wait( lock, []{return isDriveUpdated;} );
    }

    // set updated root drive hash
    rootHashPromise2.set_value( drive->rootDriveHash() );

    // wait client data infoHash (2)
    EXLOG( "@ Replicator is waiting of 2-d client data infoHash" );
    InfoHash infoHash2 = clientDataPromise2.get_future().get();
    std::cout << "\n";
    EXLOG( "@ Replicator received 2-d client data infoHash" );
    EXLOG( "@ infoHash2: " << toString(infoHash2) );
    sleep(1);

    // start drive update
    drive->startModifyDrive( infoHash2, replicatorDownloadHandler );

    // wait the end of drive update
    {
        std::unique_lock<std::mutex> lock(driveUpdateMutex);
        driveUpdateCondVar.wait( lock, []{return isDriveUpdated;} );
    }

    // wait the end of drive update
    {
        std::unique_lock<std::mutex> lock(driveUpdateMutex);
        driveUpdateCondVar.wait( lock, []{return isDriveUpdated;} );
    }

    // set updated root drive hash
    rootHashPromise3.set_value( drive->rootDriveHash() );

    // wait the end of FsTree download by client
    {
        std::unique_lock<std::mutex> lock(fsTreeMutex);
        fsTreeCondVar.wait( lock, []{return isFsTreeReceived;} );
    }
    
    EXLOG( "@ Print drive FsTree:" );
    FsTree fsTree;
    fsTree.initWithFolder( fs::path(REPLICATOR_ROOT_FOLDER) / DRIVE_PUB_KEY / "drive" );
    fsTree.dbgPrint();
    
    EXLOG( "@ Replicator exited" );
}

//
// clientDwonloadHandler
//
void clientDownloadHandler( const DownloadContext& context, download_status::code code, const std::string& /*info*/ )
{
    if ( code == download_status::complete )
    {
        EXLOG( "# Client received FsTree: " << toString(context.m_infoHash) );

        // print FsTree
        FsTree fsTree;
        fsTree.deserialize( fs::temp_directory_path() / "fsTree-folder" / FS_TREE_FILE_NAME );
        fsTree.dbgPrint();

        isFsTreeReceived = true;
        fsTreeCondVar.notify_all();
    }
    else if ( code == download_status::failed )
    {
        exit(-1);
    }
}

//
// clientDownloadFsTree
//
void clientDownloadFsTree( InfoHash rootHash, endpoint_list addrList )
{
    std::cout << "\n";
    EXLOG( "# Client started FsTree download: " << toString(rootHash) );
    auto ltSession = createDefaultSession( REPLICATOR_IP_ADDR ":5550", clientSessionErrorHandler );

    // Make the list of replicator addresses
    //
    ltSession->downloadFile( DownloadContext(
                                 clientDownloadHandler,
                                 rootHash,
                                 fs::temp_directory_path() / "fsTree-folder" ),
                             addrList );

    // wait the end of download
    {
        std::unique_lock<std::mutex> lock(fsTreeMutex);
        fsTreeCondVar.wait( lock, []{return isFsTreeReceived;} );
    }
}

//
// clientUploadFiles
//
void clientUploadFiles( endpoint_list addrList )
{
    EXLOG( "\n# Client started: 1-st upload" );

    auto ltSession = createDefaultSession( REPLICATOR_IP_ADDR ":5550", clientSessionErrorHandler );

    fs::path clientFolder = createClientFiles();

    ActionList actionList;
    actionList.push_back( Action::move( clientFolder / "a.txt", "a.txt" ) );
//    actionList.push_back( Action::upload( clientFolder / "a.txt", "a.txt" ) );
//    actionList.push_back( Action::upload( clientFolder / "a.txt", fs::path("f1") / "a_copy.txt" ) );
    actionList.push_back( Action::upload( clientFolder / "b.bin", fs::path("f1") / "b.bin" ) );
//    actionList.push_back( Action::upload( clientFolder / "b.bin", fs::path("f1") / "b2.bin" ) );
//    actionList.push_back( Action::upload( clientFolder / "b.bin", fs::path("f1") / "b3.bin" ) );
//    actionList.push_back( Action::upload( clientFolder / "b.bin", fs::path("f1") / "b_copy.bin" ) );
//    actionList.push_back( Action::upload( clientFolder / "c.txt", "c.txt" ) );
//    actionList.push_back( Action::upload( clientFolder / "c.txt", fs::path("f1") / "c_copy.txt" ) );
    actionList.dbgPrint();

    // Create empty tmp folder for 'modifyDrive data'
    //
    auto tmpFolder = fs::temp_directory_path() / "modify_drive_data";
    fs::remove_all( tmpFolder );
    fs::create_directories( tmpFolder );

    // start file downloading
    InfoHash hash = ltSession->addActionListToSession( actionList, tmpFolder, addrList );
    clientDataPromise.set_value( hash );

    EXLOG( "# Client is waiting the end of replicator update" );

    // wait the end of replicator's work
    {
        std::unique_lock<std::mutex> lock(driveUpdateMutex);
        driveUpdateCondVar.wait( lock, []{return isDriveUpdated;} );
    }

    fs::remove_all( tmpFolder );

    //EXLOG( "# Client finished" );
}

//
// clientModifyDrive
//
void clientModifyDrive( endpoint_list addrList )
{
    EXLOG( "\n# Client started: 2-d upload" );

    auto ltSession = createDefaultSession( REPLICATOR_IP_ADDR ":5550", clientSessionErrorHandler );

    // download fs tree
    
    fs::path clientFolder = createClientFiles2();

    ActionList actionList;

    // override existing file 'a.txt'
    //actionList.push_back( Action::upload( clientFolder / "a.txt", "a.txt" ) );

    // remove existing file 'a_copy.txt'
    actionList.push_back( Action::remove( "a_copy.txt" ) );

    // move 'folder1/b.bin' and upload n
//    actionList.push_back( Action::upload( clientFolder / "a.txt", "a.txt" ) );
//    actionList.push_back( Action::remove( fs::path("f1")/"b.bin" ) );
//    actionList.push_back( Action::move( "a.txt", "f1/a_moved.txt" ) );
//    actionList.push_back( Action::move( "f1/b.bin", "f2/b_moved.bin" ) );
    actionList.push_back( Action::upload( clientFolder / "a.txt", "f1/a.txt" ) );
//    actionList.push_back( Action::move( "a.txt", "f1/a_moved.txt" ) );
    actionList.push_back( Action::move( "f1", "f3/f1" ) );
//    actionList.push_back( Action::move( "/c.txt", fs::path("f1")/"c_moved.txt" ) );
//    actionList.push_back( Action::rename( fs::path("f1")/"b2.txt", "b2_moved.txt" ) );
//    actionList.push_back( Action::rename( fs::path("f1"), fs::path("f2")/"f3"/"moved_f1" ) );
//    actionList.push_back( Action::remove( fs::path("/folder11") / "bb.bin" ) );
//    actionList.push_back( Action::upload( clientFolder / "f1" / "b.bin", "folder1/b.bin" ) );
//    actionList.push_back( Action::upload( clientFolder / "c.txt", "c.txt" ) );
    actionList.dbgPrint();

    // Create empty tmp folder for 'modifyDrive data'
    //
    auto tmpFolder = fs::temp_directory_path() / "modify_drive_data";
    EXLOG( "tmp:" << fs::temp_directory_path() / "modify_drive_data" );
    fs::remove_all( tmpFolder );
    fs::create_directories( tmpFolder );

    // start file uploading
    InfoHash hash = ltSession->addActionListToSession( actionList, tmpFolder, addrList );
    clientDataPromise2.set_value( hash );

    EXLOG( "# Client is waiting the end of replicator update" );

    // wait the end of replicator's work
    {
        std::unique_lock<std::mutex> lock(driveUpdateMutex);
        driveUpdateCondVar.wait( lock, []{return isDriveUpdated;} );
    }

    fs::remove_all( tmpFolder );

    //EXLOG( "Client finished" );
}

//
// createClientFiles
//
fs::path createClientFiles() {

    // Create empty tmp folder for testing
    //
    auto tmpFolder = fs::temp_directory_path() / "client_files";
    fs::remove_all( tmpFolder );
    fs::create_directories( tmpFolder );

    {
        fs::path a_txt = tmpFolder / "a.txt";
        std::ofstream file( a_txt );
        file.write( "a_txt", 5 );
    }
    {
        fs::path b_bin = tmpFolder / "b.bin";
        fs::create_directories( b_bin.parent_path() );
        std::vector<uint8_t> data(48*1024/2);
        std::generate( data.begin(), data.end(), std::rand );
        std::ofstream file( b_bin );
        file.write( (char*) data.data(), data.size() );
    }
    {
        fs::path c_txt = tmpFolder / "c.txt";
        std::ofstream file( c_txt );
        file.write( "c_txt", 5 );
    }

    // Return path to file
    return tmpFolder;
}

//
// createClientFiles2
//
fs::path createClientFiles2() {

    // Create empty tmp folder for testing
    //
    auto tmpFolder = fs::temp_directory_path() / "client_files";
    fs::remove_all( tmpFolder );
    fs::create_directories( tmpFolder );

    {
        fs::path a_txt = tmpFolder / "a.txt";
        std::ofstream file( a_txt );
        file.write( "a_txt updated", 5 );
    }
    {
        fs::path b_bin = tmpFolder /  "b.bin";
        fs::create_directories( b_bin.parent_path() );
        std::vector<uint8_t> data(1024/2);
        std::generate( data.begin(), data.end(), std::rand );
        std::ofstream file( b_bin );
        file.write( (char*) data.data(), data.size() );
    }
    {
        fs::path c_txt = tmpFolder / "c.txt";
        std::ofstream file( c_txt );
        file.write( "new c_txt", 5 );
    }

    // Return path to file
    return tmpFolder;
}

static std::string now_str()
{
    // Get current time from the clock, using microseconds resolution
    const boost::posix_time::ptime now =
        boost::posix_time::microsec_clock::local_time();

    // Get the time offset in current day
    const boost::posix_time::time_duration td = now.time_of_day();

    //
    // Extract hours, minutes, seconds and milliseconds.
    //
    // Since there is no direct accessor ".milliseconds()",
    // milliseconds are computed _by difference_ between total milliseconds
    // (for which there is an accessor), and the hours/minutes/seconds
    // values previously fetched.
    //
    const long hours        = td.hours();
    const long minutes      = td.minutes();
    const long seconds      = td.seconds();
    const long milliseconds = td.total_milliseconds() -
                              ((hours * 3600 + minutes * 60 + seconds) * 1000);

    //
    // Format like this:
    //
    //      hh:mm:ss.SSS
    //
    // e.g. 02:15:40:321
    //
    //      ^          ^
    //      |          |
    //      123456789*12
    //      ---------10-     --> 12 chars + \0 --> 13 chars should suffice
    //
    //
    char buf[40];
    sprintf(buf, "%02ld:%02ld:%02ld.%03ld",
        hours, minutes, seconds, milliseconds);

    return buf;
}


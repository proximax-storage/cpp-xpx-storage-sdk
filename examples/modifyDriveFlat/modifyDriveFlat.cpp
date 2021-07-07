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

#define CLIENT_IP_ADDR "192.168.1.100"
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


// Replicator run loop
//
void replicator( );


// Client functions
//
void clientDownloadFsTree( InfoHash rootHash, endpoint_list addrList );
void clientModifyDrive1( endpoint_list addrList );
void clientDownloadFiles( const Folder& folder, endpoint_list addrList );
void clientModifyDrive2( endpoint_list addrList );
fs::path createClientFiles( size_t bigFileSize );

// FsTree
FsTree gFsTree;

// Client folder for his files
fs::path gTmpClientFolder;

// Libtorrent session
std::shared_ptr<Session> gClientSession = nullptr;


// global variables, which help synchronize client and replicator
//

bool                    isFileDownloaded = false;
std::condition_variable clientDownloadCondVar;
std::mutex              clientDownloadMutex;

bool                    isDriveUpdated = false;
std::condition_variable driveUpdateCondVar;
std::mutex              driveUpdateMutex;

std::promise<InfoHash>  rootHashPromise;
std::promise<InfoHash>  rootHashPromise2;
std::promise<InfoHash>  rootHashPromise3;
std::promise<InfoHash>  clientDataPromise;
std::promise<InfoHash>  clientDataPromise2;


// Listen (socket) error handle
//
void clientSessionErrorHandler( const lt::alert* alert )
{
    if ( alert->type() == lt::listen_failed_alert::alert_type )
    {
        std::cerr << alert->message() << std::endl << std::flush;
        exit(-1);
    }
}

// replicatorSessionErrorHandler
//
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

    ///
    /// Start replicator
    ///
    std::thread replicatorThread( replicator );

    ///
    /// Prepare client session
    ///
    gTmpClientFolder  = createClientFiles(10*1024);
    gClientSession = createDefaultSession( CLIENT_IP_ADDR ":5550", clientSessionErrorHandler );
    fs::path clientFolder = gTmpClientFolder / "client_files";

    ///
    /// Make the list of replicator addresses
    ///
    endpoint_list replicatorsList;
    boost::asio::ip::address e = boost::asio::ip::address::from_string(REPLICATOR_IP_ADDR);
    replicatorsList.emplace_back( e, 5001 );


    /// Client: read fsTree (1)
    ///
    clientDownloadFsTree( rootHashPromise.get_future().get(), replicatorsList );

    /// Client: request to modify drive (1)
    ///
    EXLOG( "\n# Client started: 1-st upload" );
    clientModifyDrive1( replicatorsList );

    /// Client: read changed fsTree (2)
    ///
    clientDownloadFsTree( rootHashPromise2.get_future().get(), replicatorsList );

    /// Client: read files from drive
    clientDownloadFiles( gFsTree, replicatorsList );

    /// Client: modify drive (2)
    EXLOG( "\n# Client started: 2-st upload" );
    clientModifyDrive2( replicatorsList );

    /// Client: read new fsTree (3)
    clientDownloadFsTree( rootHashPromise3.get_future().get(), replicatorsList );

    /// Delete client session
    gClientSession.reset();

    replicatorThread.join();

    fs::remove_all( gTmpClientFolder );

    return 0;
}

//
// replicatorDownloadHandler
//
void replicatorDownloadHandler ( modify_status::code code, InfoHash /*resultRootInfoHash*/, const std::string& error )
{

    if ( code == modify_status::update_completed )
    {
        EXLOG( "" );
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

    auto session = createDefaultSession( REPLICATOR_IP_ADDR":5001", replicatorSessionErrorHandler );

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
    LOG("");
    EXLOG( "@ Replicator received 1-st client data infoHash" );
    
    // start drive update
    isDriveUpdated = false;
    drive->startModifyDrive( infoHash, replicatorDownloadHandler );

    // wait the end of drive update
    {
        std::unique_lock<std::mutex> lock(driveUpdateMutex);
        driveUpdateCondVar.wait( lock, []{return isDriveUpdated;} );
    }

    drive->printDriveStatus();

    // set updated root drive hash
    isDriveUpdated = false;
    rootHashPromise2.set_value( drive->rootDriveHash() );

    // wait client data infoHash (2)
    LOG( "" );
    EXLOG( "@ Replicator is waiting of 2-d client data infoHash" );
    InfoHash infoHash2 = clientDataPromise2.get_future().get();
    std::cout << "\n";
    EXLOG( "@ Replicator received 2-d client data infoHash" );
    EXLOG( "@ infoHash2: " << toString(infoHash2) );

    // start drive update
    drive->startModifyDrive( infoHash2, replicatorDownloadHandler );

    // wait the end of drive update
    {
        std::unique_lock<std::mutex> lock(driveUpdateMutex);
        driveUpdateCondVar.wait( lock, []{return isDriveUpdated;} );
    }

    drive->printDriveStatus();

    // set updated root drive hash
    isFileDownloaded = false;
    rootHashPromise3.set_value( drive->rootDriveHash() );

    EXLOG( "@ Print drive FsTree:" );
    FsTree fsTree;
    fsTree.initWithFolder( fs::path(REPLICATOR_ROOT_FOLDER) / DRIVE_PUB_KEY / "drive" );
    fsTree.dbgPrint();
    
    // wait the end of FsTree download by client
    {
        std::unique_lock<std::mutex> lock(clientDownloadMutex);
        clientDownloadCondVar.wait( lock, []{return isFileDownloaded;} );
    }

    EXLOG( "@ Replicator exited" );
}

//
// clientDownloadHandler
//
void clientDownloadHandler( const DownloadContext& context, download_status::code code, const std::string& /*info*/ )
{
    if ( code == download_status::complete )
    {
        EXLOG( "# Client received FsTree: " << toString(context.m_infoHash) );

        gFsTree.deserialize( fs::temp_directory_path() / "fsTree-folder" / FS_TREE_FILE_NAME );

        // print FsTree
        gFsTree.dbgPrint();

        isFileDownloaded = true;
        clientDownloadCondVar.notify_all();
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
    isFileDownloaded = false;

    std::cout << "\n";
    EXLOG( "# Client started FsTree download: " << toString(rootHash) );

    // Make the list of replicator addresses
    //
    gClientSession->downloadFile( DownloadContext(
                                 clientDownloadHandler,
                                 rootHash,
                                 fs::temp_directory_path() / "fsTree-folder" ),
                             addrList );

    // wait the end of download
    {
        std::unique_lock<std::mutex> lock(clientDownloadMutex);
        clientDownloadCondVar.wait( lock, []{return isFileDownloaded;} );
    }
}

//
// clientModifyDrive1
//
void clientModifyDrive1( endpoint_list addrList )
{
    EXLOG( "\n# Client started: 1-st upload" );

    fs::path clientFolder = gTmpClientFolder / "client_files";

    ActionList actionList;
//    actionList.push_back( Action::move( clientFolder / "a.txt", "a.txt" ) );
    actionList.push_back( Action::upload( clientFolder / "a.txt", "a.txt" ) );
//    actionList.push_back( Action::upload( clientFolder / "a.txt", fs::path("f1") / "a_copy.txt" ) );
//    actionList.push_back( Action::upload( clientFolder / "b.bin", "b.bin" ) );
    actionList.push_back( Action::upload( clientFolder / "b.bin", "f1/b2.bin" ) );
//    actionList.push_back( Action::upload( clientFolder / "b.bin", fs::path("f1") / "b.bin" ) );
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
    InfoHash hash = gClientSession->addActionListToSession( actionList, tmpFolder, addrList );
    clientDataPromise.set_value( hash );

    EXLOG( "# Client is waiting the end of replicator update" );

    // wait the end of replicator's work
    {
        std::unique_lock<std::mutex> lock(driveUpdateMutex);
        driveUpdateCondVar.wait( lock, []{return isDriveUpdated;} );
    }

    //EXLOG( "# Client finished" );
}

//
// clientDownloadFilesHandler
//
void clientDownloadFilesHandler( const DownloadContext& context, download_status::code code, const std::string& error )
{
    if ( code == download_status::complete )
    {
        LOG( "@ hash: " << toString(context.m_infoHash) );
        LOG( "@ renameAs: " << context.m_renameAs );
        LOG( "@ saveFolder: " << context.m_saveFolder );
        isFileDownloaded = true;
        clientDownloadCondVar.notify_all();
    }
    else if ( code == download_status::downloading )
    {
        LOG( "downloaded:" << context.m_downloadPercents );
    }
    else if ( code == download_status::failed )
    {
        EXLOG( "# Error in clientDownloadFilesHandler: " << error );
        exit(-1);
    }
}

//
// Client: read files
//
void clientDownloadFilesR( const Folder& folder, endpoint_list addrList )
{
    for( const auto& child: folder.childs() )
    {
        if ( isFolder(child) )
        {
            //clientDownloadFiles( session, getFolder(child), addrList );
        }
        else
        {
            const File& file = getFile(child);
            EXLOG( "# Client started download file " << internalFileName( file.hash() ) );
            gClientSession->downloadFile( DownloadContext(
                                        clientDownloadFilesHandler,
                                        file.hash(),
                                        fs::temp_directory_path() / "client_tmp",
                                        fs::temp_directory_path() / "client_tmp" / file.name() ),
                                   addrList );
        }
    }
}
void clientDownloadFiles( const Folder& folder, endpoint_list addrList )
{
    isFileDownloaded = false;

    clientDownloadFilesR( folder, addrList );

    /// wait the end of file downloading
    {
        std::unique_lock<std::mutex> lock(clientDownloadMutex);
        clientDownloadCondVar.wait( lock, []{return isFileDownloaded;} );
    }
}

//
// clientModifyDrive2
//
void clientModifyDrive2( endpoint_list addrList )
{
    EXLOG( "\n# Client started: 2-d modify" );

    // download fs tree
    
    fs::path clientFolder = gTmpClientFolder / "client_files";

    ActionList actionList;

    // override existing file 'a.txt'
    //actionList.push_back( Action::upload( clientFolder / "a.txt", "a.txt" ) );

    // remove existing file 'a_copy.txt'
    //actionList.push_back( Action::remove( "a_copy.txt" ) );

    // move 'folder1/b.bin' and upload n
//    actionList.push_back( Action::upload( clientFolder / "a.txt", "a.txt" ) );
    actionList.push_back( Action::remove( "/f1/b2.bin" ) );
//    actionList.push_back( Action::move( "a.txt", "f1/a_moved.txt" ) );
//    actionList.push_back( Action::move( "f1/b.bin", "f2/b_moved.bin" ) );
//    actionList.push_back( Action::upload( clientFolder / "a.txt", "f1/a.txt" ) );
//    actionList.push_back( Action::move( "a.txt", "f1/a_moved.txt" ) );
//    actionList.push_back( Action::move( "f1", "f3/f1" ) );
//    actionList.push_back( Action::move( "/c.txt", fs::path("f1")/"c_moved.txt" ) );
//    actionList.push_back( Action::rename( fs::path("f1")/"b2.txt", "b2_moved.txt" ) );
//    actionList.push_back( Action::rename( fs::path("f1"), fs::path("f2")/"f3"/"moved_f1" ) );
//    actionList.push_back( Action::remove( fs::path("/folder11") / "bb.bin" ) );
//    actionList.push_back( Action::upload( clientFolder / "f1" / "b.bin", "folder1/b.bin" ) );
//    actionList.push_back( Action::upload( clientFolder / "c.txt", "c.txt" ) );
    actionList.dbgPrint();

    // Create empty tmp folder for 'client data'
    //
    auto tmpFolder = fs::temp_directory_path() / "modify_drive_data";
    EXLOG( "tmp:" << fs::temp_directory_path() / "modify_drive_data" );
    fs::remove_all( tmpFolder );
    fs::create_directories( tmpFolder );

    // start file uploading
    InfoHash hash = gClientSession->addActionListToSession( actionList, tmpFolder, addrList );
    clientDataPromise2.set_value( hash );

    EXLOG( "# Client is waiting the end of replicator update" );

    // wait the end of replicator's work
    {
        std::unique_lock<std::mutex> lock(driveUpdateMutex);
        driveUpdateCondVar.wait( lock, []{return isDriveUpdated;} );
    }

    //EXLOG( "Client finished" );
}

//
// createClientFiles
//
fs::path createClientFiles( size_t bigFileSize ) {

    // Create empty tmp folder for testing
    //
    auto dataFolder = fs::temp_directory_path() / "client_tmp_folder" / "client_files";
    fs::remove_all( dataFolder );
    fs::create_directories( dataFolder );

    {
        std::ofstream file( dataFolder / "a.txt" );
        file.write( "a_txt", 5 );
    }
    {
        fs::path b_bin = dataFolder / "b.bin";
        fs::create_directories( b_bin.parent_path() );
//        std::vector<uint8_t> data(10*1024*1024);
        std::vector<uint8_t> data(bigFileSize);
        std::generate( data.begin(), data.end(), std::rand );
        std::ofstream file( b_bin );
        file.write( (char*) data.data(), data.size() );
    }
    {
        std::ofstream file( dataFolder / "c.txt" );
        file.write( "c_txt", 5 );
    }
    {
        std::ofstream file( dataFolder / "d.txt" );
        file.write( "d_txt", 5 );
    }

    // Return path to file
    return dataFolder.parent_path();
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


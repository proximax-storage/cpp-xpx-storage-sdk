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
static void replicator( );

//
// Client functions
//
static fs::path createClientFiles( size_t bigFileSize );
static void     clientDownloadFsTree( endpoint_list addrList );
static void     clientModifyDrive( const ActionList&, endpoint_list addrList );
static void     clientDownloadFiles( int fileNumber, Folder& folder, endpoint_list addrList );

// FsTree
FsTree gFsTree;

// Client folder for his files
fs::path gTmpClientFolder;

// Libtorrent session
std::shared_ptr<Session> gClientSession = nullptr;


//
// global variables, which help synchronize client and replicator
//

bool                        isDownloadCompleted = false;
std::shared_ptr<InfoHash>   clientModifyHash;
std::condition_variable     clientCondVar;
std::mutex                  clientMutex;

bool                        isDriveUpdated = false;
std::shared_ptr<InfoHash>   driveRootHash;
std::condition_variable     driveCondVar;
std::mutex                  driveMutex;

bool                        stopReplicator = false;


// Listen (socket) error handle
//
static void clientSessionErrorHandler( const lt::alert* alert )
{
    if ( alert->type() == lt::listen_failed_alert::alert_type )
    {
        std::cerr << alert->message() << std::endl << std::flush;
        exit(-1);
    }
}

// replicatorSessionErrorHandler
//
static void replicatorSessionErrorHandler( const lt::alert* alert)
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
    clientDownloadFsTree(replicatorsList );

    /// Client: request to modify drive (1)
    ///
    EXLOG( "\n# Client started: 1-st upload" );
    {
        ActionList actionList;
        actionList.push_back( Action::upload( clientFolder / "a.txt", "a.txt" ) );
        actionList.push_back( Action::upload( clientFolder / "a.txt", "a2.txt" ) );
        actionList.push_back( Action::upload( clientFolder / "b.bin", "f1/b1.bin" ) );
        actionList.push_back( Action::upload( clientFolder / "b.bin", "f2/b2.bin" ) );
        actionList.push_back( Action::upload( clientFolder / "a.txt", "f2/a.txt" ) );
        clientModifyDrive( actionList, replicatorsList );
    }

    /// Client: read changed fsTree (2)
    ///
    clientDownloadFsTree(replicatorsList );

    /// Client: read files from drive
    clientDownloadFiles( 5, gFsTree, replicatorsList );

    /// Client: modify drive (2)
    EXLOG( "\n# Client started: 2-st upload" );
    {
        ActionList actionList;
        actionList.push_back( Action::remove( "a2.txt" ) );
        actionList.push_back( Action::remove( "f1/b2.bin" ) );
        actionList.push_back( Action::remove( "f2/b2.bin" ) );
        clientModifyDrive( actionList, replicatorsList );
    }

    /// Client: read new fsTree (3)
    clientDownloadFsTree(replicatorsList );

    /// Delete client session
    gClientSession.reset();

    /// Stop Replicator
    stopReplicator = true;
    clientModifyHash = std::make_shared<InfoHash>();
    clientCondVar.notify_all();

    replicatorThread.join();

    fs::remove_all( gTmpClientFolder );

    return 0;
}

//
// replicatorDownloadHandler
//
static void replicatorDownloadHandler ( modify_status::code code, InfoHash /*resultRootInfoHash*/, const std::string& error )
{

    if ( code == modify_status::update_completed )
    {
        EXLOG( "" );
        EXLOG( "@ update_completed" );

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
static void replicator()
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
    {
        std::lock_guard locker(driveMutex);
        driveRootHash = std::make_shared<InfoHash>( drive->rootDriveHash() );
    }
    driveCondVar.notify_all();


    for( int i=1; ; i++ )
    {
        EXLOG( "@ Replicator is waiting of client data infoHash (" << i << ")");
        EXLOG(   "- - - - - - - - - - - - - - - - - - - - - - - - ");
        {
            std::unique_lock<std::mutex> lock(clientMutex);
            clientCondVar.wait( lock, []{ return clientModifyHash;} );
        }

        InfoHash modifyHash = *clientModifyHash;
        clientModifyHash.reset();

        if ( stopReplicator )
            break;

        EXLOG( "@ Replicator received client data infoHash (" << i << ")" );

        // start drive update
        isDriveUpdated = false;
        drive->startModifyDrive( modifyHash, replicatorDownloadHandler );

        // wait the end of drive update
        {
            std::unique_lock<std::mutex> lock(driveMutex);
            driveCondVar.wait( lock, [] { return isDriveUpdated; } );
        }

        drive->printDriveStatus();

        // set root drive hash
        driveRootHash = std::make_shared<InfoHash>( drive->rootDriveHash() );
        driveCondVar.notify_all();
    }

    EXLOG( "@ Replicator exited" );
}

//
// clientDownloadHandler
//
static void clientDownloadHandler( const DownloadContext& context,
                                   download_status::code  code,
                                   float /*downloadPercents*/,
                                   const std::string& /*info*/ )
{
    if ( code == download_status::complete )
    {
        EXLOG( "# Client received FsTree: " << toString(context.m_infoHash) );
        EXLOG( "# FsTree: " << gTmpClientFolder / "fsTree-folder" / FS_TREE_FILE_NAME );
        gFsTree.deserialize( gTmpClientFolder / "fsTree-folder" / FS_TREE_FILE_NAME );

        // print FsTree
        gFsTree.dbgPrint();

        isDownloadCompleted = true;
        clientCondVar.notify_all();
    }
    else if ( code == download_status::failed )
    {
        exit(-1);
    }
}

//
// clientDownloadFsTree
//
static void clientDownloadFsTree( endpoint_list addrList )
{
    // wait drive root hash
    {
        std::unique_lock<std::mutex> lock(driveMutex);
        driveCondVar.wait( lock, [] { return driveRootHash; } );
    }

    InfoHash rootHash = *driveRootHash;
    driveRootHash.reset();

    isDownloadCompleted = false;

    LOG("");
    EXLOG( "# Client started FsTree download: " << toString(rootHash) );

    gClientSession->downloadFile( DownloadContext(
                                 clientDownloadHandler,
                                 rootHash,
                                 gTmpClientFolder / "fsTree-folder" ),
                             addrList );

    // wait the end of download
    {
        std::unique_lock<std::mutex> lock(clientMutex);
        clientCondVar.wait( lock, []{ return isDownloadCompleted; } );
    }
}

//
// clientModifyDrive
//
static void clientModifyDrive( const ActionList& actionList, endpoint_list addrList )
{
    actionList.dbgPrint();

    // Create empty tmp folder for 'client modify data'
    //
    auto tmpFolder = fs::temp_directory_path() / "modify_drive_data";
    fs::remove_all( tmpFolder );
    fs::create_directories( tmpFolder );

    // start file uploading
    InfoHash hash = gClientSession->addActionListToSession( actionList, tmpFolder, addrList );

    // inform replicator
    clientModifyHash = std::make_shared<InfoHash>( hash );
    clientCondVar.notify_all();

    EXLOG( "# Client is waiting the end of replicator update" );

    // wait the end of replicator's work
    {
        std::unique_lock<std::mutex> lock(driveMutex);
        driveCondVar.wait( lock, []{ return isDriveUpdated; } );
    }
}

//
// clientDownloadFilesHandler
//
int downloadFileCount;
int downloadedFileCount;
static void clientDownloadFilesHandler( const DownloadContext& /*context*/,
                                        download_status::code  code,
                                        float                  downloadPercents,
                                        const std::string&     error )
{
    if ( code == download_status::complete )
    {
//        LOG( "@ hash: " << toString(context.m_infoHash) );
//        LOG( "@ renameAs: " << context.m_renameAs );
//        LOG( "@ saveFolder: " << context.m_saveFolder );
        if ( ++downloadedFileCount == downloadFileCount )
        {
            EXLOG( "# Downloaded " << downloadedFileCount << " files" );
            isDownloadCompleted = true;
            clientCondVar.notify_all();
        }
    }
    else if ( code == download_status::downloading )
    {
        LOG( "downloaded:" << downloadPercents );
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
static void clientDownloadFilesR( const Folder& folder, endpoint_list addrList )
{
    for( const auto& child: folder.childs() )
    {
        if ( isFolder(child) )
        {
            clientDownloadFilesR( getFolder(child), addrList );
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
static void clientDownloadFiles( int fileNumber, Folder& fsTree, endpoint_list addrList )
{
    isDownloadCompleted = false;

    downloadFileCount = 0;
    downloadedFileCount = 0;
    fsTree.iterate([](File& /*file*/) {
        downloadFileCount++;
    });

    if ( downloadFileCount == 0 )
    {
        EXLOG( "downloadFileCount == 0" );
        return;
    }

    if ( fileNumber != downloadFileCount )
    {
        EXLOG( "!ERROR! clientDownloadFiles(): fileNumber != downloadFileCount; " << fileNumber <<"!=" << downloadFileCount );
        exit(-1);
    }

    EXLOG("#======================clientDownloadFiles= " << downloadFileCount );

    clientDownloadFilesR( fsTree, addrList );

    /// wait the end of file downloading
    {
        std::unique_lock<std::mutex> lock(clientMutex);
        clientCondVar.wait( lock, [] { return isDownloadCompleted; } );
    }
}


//
// createClientFiles
//
static fs::path createClientFiles( size_t bigFileSize ) {

    // Create empty tmp folder for testing
    //
    auto dataFolder = fs::temp_directory_path() / "client_tmp_folder" / "client_files";
    fs::remove_all( dataFolder.parent_path() );
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


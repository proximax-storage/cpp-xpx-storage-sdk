#include "drive/RpcReplicatorClient.h"

#include "drive/Session.h"
#include "drive/FsTree.h"
#include "drive/Utils.h"

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

#define CLIENT_ADDR "192.168.1.102" ":5551"
#define REPLICATOR_IP "127.0.0.1"
#define REPLICATOR_PORT 5550

namespace fs = std::filesystem;

using namespace sirius::drive;

inline std::mutex gExLogMutex;

static std::string now_str();

#define EXLOG(expr) { \
        const std::lock_guard<std::mutex> autolock( gExLogMutex ); \
        std::cerr << now_str() << ": " << expr << std::endl << std::flush; \
    }

//
// Client functions
//
static fs::path createClientFiles( size_t bigFileSize );
static void     clientDownloadFsTree( endpoint_list addrList, const InfoHash& );
static InfoHash clientModifyDrive( const ActionList&, endpoint_list addrList );
static void     clientDownloadFiles( int fileNumber, Folder& folder, endpoint_list addrList );

// FsTree
FsTree gFsTree;

// Client folder for his files
fs::path gClientFolder;

// Libtorrent session
std::shared_ptr<Session> gClientSession = nullptr;

//
// global variables, which help synchronize client
//
bool                        isDownloadCompleted = false;
std::condition_variable     clientCondVar;
std::mutex                  clientMutex;


// Listen (socket) error handle
static void clientSessionErrorHandler( const lt::alert* alert )
{
    if ( alert->type() == lt::listen_failed_alert::alert_type )
    {
        std::cerr << alert->message() << std::endl << std::flush;
        std::cerr << "cannot open server socket at: " << CLIENT_ADDR << std::endl << std::flush;
        exit(-1);
    }
}


int main() try {
    EXLOG("client started");

    ///
    /// Prepare client session
    ///
    gClientFolder  = createClientFiles(10*1024);
    gClientSession = createDefaultSession( CLIENT_ADDR, clientSessionErrorHandler );
    fs::path clientFolder = gClientFolder / "client_files";

    ///
    /// Make the list of replicator addresses
    ///
    endpoint_list replicatorsList;
    boost::asio::ip::address ip = boost::asio::ip::address::from_string(REPLICATOR_IP);
    replicatorsList.emplace_back( ip, REPLICATOR_PORT );

    //
    // RpcClient
    //
    RpcReplicatorClient client( REPLICATOR_IP, 5510 );

    // Create drive
    sirius::Key driveKey{ {0x01,0x01} };
    client.addDrive( driveKey, 1024 );

    // Download fsTree
    InfoHash rootHash = client.getRootHash( driveKey );
//    LOG( "rootHash=" << toString(rootHash) );
//    rootHash[0] = 0;
    LOG( "rootHash=" << toString(rootHash) );
    clientDownloadFsTree(replicatorsList, rootHash );
    return 0;


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
        InfoHash hash = clientModifyDrive( actionList, replicatorsList );
        client.modify( driveKey, hash );
    }

    /// Client: read changed fsTree (2)
    ///
    rootHash = client.getRootHash( driveKey );
    clientDownloadFsTree(replicatorsList, rootHash );

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
    rootHash = client.getRootHash( driveKey );
    clientDownloadFsTree(replicatorsList, rootHash );

    /// Delete client session
    gClientSession.reset();

    return 0;
}
catch( const std::runtime_error& err )
{
    LOG( "ERROR: " << err.what() );
    exit(-1);
}

//
// clientDownloadHandler
//
static void clientDownloadHandler( download_status::code code,
                                   const InfoHash& infoHash,
                                   const std::filesystem::path /*filePath*/,
                                   size_t /*downloaded*/,
                                   size_t /*fileSize*/,
                                   const std::string& /*errorText*/ )
{
    if ( code == download_status::complete )
    {
        EXLOG( "# Client received FsTree: " << toString(infoHash) );
        //EXLOG( "# FsTree file path: " << filePath );
        gFsTree.deserialize( gClientFolder / "fsTree-folder" / FS_TREE_FILE_NAME );

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
static void clientDownloadFsTree( endpoint_list addrList, const InfoHash& rootHash )
{
    LOG("clientDownloadFsTree()" << " " << addrList[0].address() << ":" << addrList[0].port() );

    isDownloadCompleted = false;

    EXLOG( "# Client started FsTree download: " << toString(rootHash) );

    gClientSession->download( DownloadContext(
            DownloadContext::fs_tree,
            clientDownloadHandler,
            rootHash ),
                              gClientFolder / "fsTree-folder",
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
static InfoHash clientModifyDrive( const ActionList& actionList, endpoint_list addrList )
{
    actionList.dbgPrint();

    // Create empty tmp folder for 'client modify data'
    //
    auto tmpFolder = fs::temp_directory_path() / "modify_drive_data";
    fs::remove_all( tmpFolder );
    fs::create_directories( tmpFolder );

    // prepare file uploading
    InfoHash hash = gClientSession->addActionListToSession( actionList, tmpFolder, addrList );

    return hash;
}

//
// clientDownloadFilesHandler
//
int downloadFileCount;
int downloadedFileCount;
static void clientDownloadFilesHandler( download_status::code code,
                                        const InfoHash& /*infoHash*/,
                                        const std::filesystem::path filePath,
                                        size_t downloaded,
                                        size_t fileSize,
                                        const std::string& errorText )
{
    if ( code == download_status::complete )
    {
//        LOG( "@ hash: " << toString(context.m_infoHash) );
//        LOG( "@ renameAs: " << context.m_renameAs );
//        LOG( "@ saveFolder: " << context.m_saveFolder );
        if ( ++downloadedFileCount == downloadFileCount )
        {
            EXLOG( "# Downloaded " << filePath << " files" );
            isDownloadCompleted = true;
            clientCondVar.notify_all();
        }
    }
    else if ( code == download_status::downloading )
    {
        LOG( "downloading: " << downloaded << " of " << fileSize );
    }
    else if ( code == download_status::failed )
    {
        EXLOG( "# Error in clientDownloadFilesHandler: " << errorText );
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
            EXLOG( "#  to " << gClientFolder / "downloaded_files" / folder.name()  / file.name() );
            gClientSession->download( DownloadContext(
                    DownloadContext::file_from_drive,
                    clientDownloadFilesHandler,
                    file.hash(),
                    gClientFolder / "downloaded_files" / folder.name() / file.name() ),
                                      gClientFolder / "downloaded_files",
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
#define REPLICATOR_ROOT_FOLDER          (std::string(getenv("HOME"))+"/111/replicator_root")

    auto dataFolder = fs::path(getenv("HOME")) / "111" / "client_tmp_folder" / "client_files";
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


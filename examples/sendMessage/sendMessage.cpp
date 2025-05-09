#include "../../src/drive/Session.h"
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

#ifdef SIRIUS_DRIVE_MULTI
#include <sirius_drive/session_delegate.h>
#endif


//
// This example shows interaction between 'client' and 'replicator'.
//

// !!!
// CLIENT_IP_ADDR should be changed to proper address according to your network settings (see ifconfig)

#define CLIENT_IP_ADDR "192.168.1.102"
#define REPLICATOR_IP_ADDR "127.0.0.1"
#define REPLICATOR_ROOT_FOLDER          fs::path(getenv("HOME")) / "111" / "replicator_root"
#define REPLICATOR_SANDBOX_ROOT_FOLDER  fs::path(getenv("HOME")) / "111" / "sandbox_root"
#define DRIVE_PUB_KEY                   "pub_key"

#define CLIENT_WORK_FOLDER              fs::path(getenv("HOME")) / "111" / "client_work_folder"

namespace fs = std::filesystem;

using namespace sirius::drive;

inline std::mutex gExLogMutex;

#define EXLOG(expr) { \
        const std::lock_guard<std::mutex> autolock( gExLogMutex ); \
        std::cout << << expr << std::endl << std::flush; \
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
fs::path gClientFolder;

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

#ifdef SIRIUS_DRIVE_MULTI
class SimpleDownloadLimiter : public lt::session_delegate
{
    bool checkDownloadLimit( std::vector<uint8_t> /*reciept*/,
                             lt::sha256_hash /*downloadChannelId*/,
                             size_t /*downloadedSize*/ ) override
    {
        return true;
    }

    const lt::sha256_hash& privateKey() override
    {
        return reinterpret_cast<const lt::sha256_hash&>(m_privateKey);
    }

    const lt::sha256_hash& publicKey() override
    {
        return reinterpret_cast<const lt::sha256_hash&>(m_publicKey);
    }

private:
    sirius::Hash256 m_privateKey;
    sirius::Hash256 m_publicKey;
};
#endif

//
// main
//
int main(int,char**)
{
    ///
    /// Start replicator
    ///
    std::thread replicatorThread( replicator );
    //todo!!!
    //sleep(1000);

    ///
    /// Prepare client session
    ///
    gClientFolder  = createClientFiles(10*1024);
    LOG( "gClientFolder: " << gClientFolder );
    gClientSession = createDefaultSession( CLIENT_IP_ADDR ":5550", clientSessionErrorHandler
#ifdef SIRIUS_DRIVE_MULTI
    ,std::make_shared<SimpleDownloadLimiter>()
#endif
    );
    
    fs::path clientFolder = gClientFolder / "client_files";

    ///
    /// Make the list of replicator addresses
    ///
    endpoint_list replicatorsList;
//    replicatorsList.emplace_back( e, 5001 );

    // wait drive root hash
    {
        std::unique_lock<std::mutex> lock(driveMutex);
        driveCondVar.wait( lock, [] { return driveRootHash; } );
    }

    auto addr = boost::asio::ip::address::from_string(REPLICATOR_IP_ADDR);
    boost::asio::ip::udp::endpoint udp( addr, 5001 );
    gClientSession->sendMessage( udp, std::vector<uint8_t>() );


    /// Client: read fsTree (1)
    ///
    clientDownloadFsTree(replicatorsList );

    /// Client: request to modify drive (1)
    ///
    EXLOG( "\n# Client started: 1-st upload" );
    {
        ActionList actionList;
        actionList.push_back( Action::newFolder( "fff1/" ) );
        actionList.push_back( Action::newFolder( "fff1/ffff1" ) );
        actionList.push_back( Action::upload( clientFolder / "a.txt", "fff2/a.txt" ) );

        //actionList.push_back( Action::upload( clientFolder / "a.txt", "a.txt" ) );
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
        //actionList.push_back( Action::move( "fff1/", "fff1/ffff1" ) );
        actionList.push_back( Action::remove( "fff1/" ) );
        actionList.push_back( Action::remove( "fff2/" ) );

        actionList.push_back( Action::remove( "a2.txt" ) );
        actionList.push_back( Action::remove( "f1/b2.bin" ) );
//        actionList.push_back( Action::remove( "f1" ) );
        actionList.push_back( Action::remove( "f2/b2.bin" ) );
        actionList.push_back( Action::move( "f2/", "f2_renamed/" ) );
        actionList.push_back( Action::move( "f2_renamed/a.txt", "f2_renamed/a_renamed.txt" ) );
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

    //fs::remove_all( gClientFolder );

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

    auto session = createDefaultSession( REPLICATOR_IP_ADDR":5001", replicatorSessionErrorHandler
#ifdef SIRIUS_DRIVE_MULTI
            ,std::make_shared<SimpleDownloadLimiter>()
#endif
    );

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
        EXLOG( "# FsTree file path: " << gClientFolder / "fsTree-folder" / FS_TREE_FILE_NAME );
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
static void clientDownloadFsTree( endpoint_list addrList )
{
    // wait drive root hash
    {
        std::unique_lock<std::mutex> lock(driveMutex);
        driveCondVar.wait( lock, [] { return driveRootHash; } );
    }

    InfoHash rootHash = *driveRootHash;
    //todo!!!
    //rootHash[0] = 0;
    driveRootHash.reset();

    isDownloadCompleted = false;

    LOG("");
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
            std::string folderName = "root";
            if ( folder.name() != "/" )
                folderName = folder.name();
            EXLOG( "# Client started download file " << internalFileName( file.hash() ) );
            EXLOG( "#  to " << gClientFolder / "downloaded_files" / folderName  / file.name() );
            gClientSession->download( DownloadContext(
                                            DownloadContext::file_from_drive,
                                            clientDownloadFilesHandler,
                                            file.hash(),
                                            gClientFolder / "downloaded_files" / folderName / file.name() ),
                                            //gClientFolder / "downloaded_files" / folderName / toString(file.hash()) ),
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
    auto dataFolder = CLIENT_WORK_FOLDER / "client_files";
    fs::remove_all( dataFolder.parent_path() );
    fs::create_directories( dataFolder );
    //fs::create_directories( dataFolder/"empty_folder" );

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

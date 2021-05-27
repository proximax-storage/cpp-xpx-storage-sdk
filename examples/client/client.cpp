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

#define CLIENT_IP_ADDR     "192.168.1.100"
#define REPLICATOR_IP_ADDR "127.0.0.1"

namespace fs = std::filesystem;

using namespace sirius::drive;

inline std::mutex gExLogMutex;

#define EXLOG(expr) { \
        const std::lock_guard<std::mutex> autolock( gLogMutex ); \
        std::cout << expr << std::endl << std::flush; \
    }


// forward declarations
//

// global variables, which help synchronize client and replicator
//
bool                        isDownloadCompleted = false;
std::condition_variable     clientCondVar;
std::mutex                  clientMutex;
std::shared_ptr<InfoHash>   driveRootHash;

//
// Client functions
//
static fs::path createClientFiles( size_t bigFileSize );
static void     clientDownloadFsTree( endpoint_list addrList );
static InfoHash clientModifyDrive( const ActionList&, endpoint_list addrList );

// FsTree
FsTree gFsTree;

// Client folder for his files
fs::path gTmpClientFolder;

// Libtorrent session
std::shared_ptr<Session> gClientSession = nullptr;


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


//
// main
//
int main(int,char**)
{

    ///
    /// Prepare client session
    ///
    gTmpClientFolder  = createClientFiles(10*1024);
    gClientSession = createDefaultSession( CLIENT_IP_ADDR ":5511", clientSessionErrorHandler );
    fs::path clientFolder = gTmpClientFolder / "client_files";

    ///
    /// Make the list of replicator addresses
    ///
    endpoint_list replicatorsList;
    boost::asio::ip::address e = boost::asio::ip::address::from_string(REPLICATOR_IP_ADDR);
    replicatorsList.emplace_back( e, 5550 );


    //
    // Create client socket
    //

    boost::asio::io_service io_service;
    boost::asio::ip::tcp::socket client_socket(io_service);

    client_socket.connect(
                tcp::endpoint(
                    boost::asio::ip::address::from_string("127.0.0.1"),
                    9999));

    //
    // Read root hash
    //
    boost::system::error_code ec;
    boost::asio::streambuf buf;
    boost::asio::read( client_socket, buf, boost::asio::transfer_exactly(32), ec );
    if (ec) {
        EXLOG( "boost::asio::read error:" << ec.message() );
        exit(-1);
    }

    InfoHash infoHash;
    memcpy( &infoHash[0], boost::asio::buffer_cast<const void*>(buf.data()), 32 );
    driveRootHash = std::make_shared<InfoHash>( infoHash );
    LOG( "Received driveRootHash: " << driveRootHash );

    //
    // Download FsTree
    //
    clientDownloadFsTree(replicatorsList );

    /// Client: request to modify drive (1)
    ///
    InfoHash modifyInfoHash;
    EXLOG( "\n# Client started: 1-st upload" );
    {
        ActionList actionList;
        actionList.push_back( Action::upload( clientFolder / "a.txt", "a.txt" ) );
        //actionList.push_back( Action::upload( clientFolder / "a.txt", "a2.txt" ) );
        actionList.push_back( Action::upload( clientFolder / "b.bin", "b.bin" ) );
        //actionList.push_back( Action::upload( clientFolder / "b.bin", "f2/b2.bin" ) );
        //actionList.push_back( Action::upload( clientFolder / "a.txt", "f2/a.txt" ) );
        modifyInfoHash = clientModifyDrive( actionList, replicatorsList );
    }

    EXLOG( "# modifyInfoHash: " << toString(modifyInfoHash) );

    boost::asio::write( client_socket, boost::asio::buffer( modifyInfoHash ), ec );
    if (ec) {
        EXLOG( "boost::asio::write error:" << ec.message() );
        exit(-1);
    }

    EXLOG( "# Client is waiting the end of replicator update" );


    //
    // Read root hash
    //
    boost::asio::streambuf buf2;
    boost::asio::read( client_socket, buf2, boost::asio::transfer_exactly(32), ec );
    if (ec) {
        EXLOG( "boost::asio::read(2) error:" << ec.message() );
        exit(-1);
    }

    memcpy( &infoHash[0], boost::asio::buffer_cast<const void*>(buf2.data()), 32 );
    driveRootHash = std::make_shared<InfoHash>( infoHash );
    *driveRootHash = infoHash;
    EXLOG( "Received driveRootHash (2): " << toString(infoHash) );
    EXLOG( "Received driveRootHash (2): " << toString(*driveRootHash) );

    /// Client: read changed fsTree (2)
    ///
    clientDownloadFsTree(replicatorsList );

    EXLOG( "# Client finished");

    return 0;
}

//
// clientDwonloadHandler
//
void clientDownloadHandler( download_status::code code, const InfoHash& hash, const std::string& /*info*/ )
{
    if ( code == download_status::complete )
    {
        EXLOG( "# Client received FsTree: " << toString(hash) );

        // print FsTree
        FsTree fsTree;
        fsTree.deserialize( fs::temp_directory_path() / "fsTree-folder" / FS_TREE_FILE_NAME );
        fsTree.dbgPrint();

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

    InfoHash rootHash = *driveRootHash;
    //todo!!!
    //rootHash[0] = 0;
    driveRootHash.reset();

    isDownloadCompleted = false;

    LOG("");
    EXLOG( "# Client started FsTree download: " << toString(rootHash) );

    gClientSession->downloadFile( rootHash,
                             fs::temp_directory_path() / "fsTree-folder",
                             clientDownloadHandler,
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
    //actionList.dbgPrint();

    // Create empty tmp folder for 'client modify data'
    //
    auto tmpFolder = fs::temp_directory_path() / "modify_drive_data";
    fs::remove_all( tmpFolder );
    fs::create_directories( tmpFolder );

    // start file uploading
    InfoHash hash = gClientSession->addActionListToSession( actionList, tmpFolder, addrList );

    return hash;
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



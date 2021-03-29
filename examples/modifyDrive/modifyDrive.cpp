#include "LibTorrentSession.h"
#include "Drive.h"

#include <fstream>
#include <filesystem>
#include <future>
#include <condition_variable>

#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>

//
// This example shows how 'client' downloads file from 'replicator'.
//

// CLIENT_IP_ADDR should be changed to proper address according to your network settings (see ifconfig)
// !!!
#define CLIENT_IP_ADDR "192.168.1.100"
#define REPLICATOR_IP_ADDR "127.0.0.1"
#define REPLICATOR_ROOT_FOLDER          "/Users/alex/111/test_replicator_root"
#define REPLICATOR_SANDBOX_ROOT_FOLDER  "/Users/alex/111/test_replicator_sandbox_root"
#define DRIVE_PUB_KEY                   "test_drive_pub_key"

namespace fs = std::filesystem;

using namespace sirius::drive;

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
std::mutex              clientPauseMutex;
std::promise<InfoHash>  rootHashPromise;
std::promise<InfoHash>  rootHashPromise2;
std::promise<InfoHash>  uploadPromise;
std::promise<InfoHash>  modifyPromise;

// alertHandler
void alertHandler( LibTorrentSession*, libtorrent::alert* alert )
{
    if ( alert->type() == lt::listen_failed_alert::alert_type )
    {
        std::cerr << alert->message() << std::endl;
        exit(-1);
    }
}

//
// main
//
int main(int,char**)
{
    // prepare mutex for unlocking
    clientPauseMutex.lock();

    // Start replicator
    std::thread replicatorThread( replicator );

    // Make the list of replicator addresses
    //
    endpoint_list replicatorsList;
    boost::asio::ip::address e = boost::asio::ip::address::from_string(CLIENT_IP_ADDR);
    replicatorsList.emplace_back( e, 5551 );

    // Client: read fsTree
    clientDownloadFsTree( rootHashPromise.get_future().get(), replicatorsList );

    // Client: upload files
    clientUploadFiles( replicatorsList );

    // Client: read new fsTree
    clientDownloadFsTree( rootHashPromise2.get_future().get(), replicatorsList );

    // Client: modify drive
    clientModifyDrive( replicatorsList );

    replicatorThread.join();

    return 0;
}

//
// replicatorDownloadHandler
//
void replicatorDownloadHandler ( modify_status::code code, InfoHash resultRootInfoHash, std::string error )
{

    if ( code == modify_status::update_completed ) {
        std::cout << "successfully uploaded" << std::endl;
    }
    else if ( code == modify_status::calculated ) {
        std::cout << "successfully uploaded" << std::endl;
    }
    else {
        std::cout << "ERROR: " << error << std::endl;
    }

    // unlock client
    clientPauseMutex.unlock();
}

//
// replicator
//
void replicator()
{
    std::cout << "Replicator started" << std::endl;

    fs::remove_all( REPLICATOR_ROOT_FOLDER );
    fs::remove_all( REPLICATOR_SANDBOX_ROOT_FOLDER );
    auto drive = createDefaultDrive( CLIENT_IP_ADDR":5551",
                                     REPLICATOR_ROOT_FOLDER,
                                     REPLICATOR_SANDBOX_ROOT_FOLDER,
                                     DRIVE_PUB_KEY,
                                     100*1024*1024, {} );

    rootHashPromise.set_value( drive->rootDriveHash() );

    std::cout << "Replicator is waiting of infoHash" << std::endl;
    InfoHash infoHash = uploadPromise.get_future().get();

    std::cout << "Replicator received infoHash" << std::endl;
    
    drive->startModifyDrive( infoHash, replicatorDownloadHandler );

    rootHashPromise2.set_value( drive->rootDriveHash() );

    std::cout << "Replicator is waiting of 2-d infoHash" << std::endl;
    InfoHash infoHash2 = modifyPromise.get_future().get();

    std::cout << "Replicator received 2-d infoHash" << std::endl;

    drive->startModifyDrive( infoHash2, replicatorDownloadHandler );

    sleep(1);
}

//
// clientDwonloadHandler
//
void clientDownloadHandler( download_status::code code, InfoHash, std::string info )
{
    if ( code == download_status::complete )
    {
//        FsTree fsTree;
//        fsTree.deserialize( fs::temp_directory_path() / "fsTree-folder" / FS_TREE_FILE_NAME );
//        fsTree.dbgPrint();

        clientPauseMutex.unlock();
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
    auto ltSession = createDefaultLibTorrentSession( REPLICATOR_IP_ADDR ":5550", alertHandler );

    // Make the list of replicator addresses
    //
    ltSession->downloadFile( rootHash,
                             fs::temp_directory_path() / "fsTree-folder",
                             clientDownloadHandler,
                             addrList );
    // wait the end of download
    clientPauseMutex.lock();
}

//
// clientUploadFiles
//
void clientUploadFiles( endpoint_list addrList )
{
    std::cout << "Client started" << std::endl;

    auto ltSession = createDefaultLibTorrentSession( REPLICATOR_IP_ADDR ":5550", alertHandler );

    fs::path clientFolder = createClientFiles();

    ActionList actionList;
    actionList.push_back( Action::upload( clientFolder / "a.txt", "a.txt" ) );
    actionList.push_back( Action::upload( clientFolder / "a.txt", fs::path("folder1") / "a_copy.txt" ) );
    actionList.push_back( Action::upload( clientFolder / "b.bin", fs::path("folder1") / "b.bin" ) );
    actionList.push_back( Action::upload( clientFolder / "b.bin", fs::path("folder1") / "b_copy.bin" ) );
    actionList.push_back( Action::upload( clientFolder / "c.txt", "c.txt" ) );
    actionList.push_back( Action::upload( clientFolder / "c.txt", fs::path("folder1") / "c_copy.txt" ) );

    // Create empty tmp folder for 'modifyDrive data'
    //
    auto tmpFolder = fs::temp_directory_path() / "modify_drive_data";
    fs::remove_all( tmpFolder );
    fs::create_directories( tmpFolder );

    // start file downloading
    InfoHash hash = ltSession->addActionListToSession( actionList, tmpFolder, addrList );
    uploadPromise.set_value( hash );

    std::cout << "Client is waiting pauseMutex.unlock" << std::endl;

    // wait the end of replicator's work
    clientPauseMutex.lock();

    fs::remove_all( tmpFolder );

    std::cout << "Client finished" << std::endl;
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

//
// clientModifyDrive
//
void clientModifyDrive( endpoint_list addrList )
{
    std::cout << "Client started" << std::endl;

    auto ltSession = createDefaultLibTorrentSession( REPLICATOR_IP_ADDR ":5550", alertHandler );

    // download fs tree

    fs::path clientFolder = createClientFiles2();

    ActionList actionList;

    // override existing file 'a.txt'
    actionList.push_back( Action::upload( clientFolder / "a.txt", "a.txt" ) );

    // remove existing file 'a_copy.txt'
    actionList.push_back( Action::remove( "a_copy.txt" ) );

    // move 'folder1/b.bin' and upload n
    actionList.push_back( Action::remove( fs::path("folder1")/"b.bin" ) );
    actionList.push_back( Action::rename( "c.txt", fs::path("folder1")/"c_moved.txt" ) );

    actionList.push_back( Action::rename( "c.txt", fs::path("folder1")/"c_moved.txt" ) );

    actionList.push_back( Action::remove( clientFolder / "folder11" / "bb.bin" ) );//, "folder11/b.bin" ) );
    actionList.push_back( Action::upload( clientFolder / "folder1" / "b.bin", "folder1/b.bin" ) );
    actionList.push_back( Action::upload( clientFolder / "c.txt", "c.txt" ) );

    // Create empty tmp folder for 'modifyDrive data'
    //
    auto tmpFolder = fs::temp_directory_path() / "modify_drive_data";
    fs::remove_all( tmpFolder );
    fs::create_directories( tmpFolder );

    // start file downloading
    InfoHash hash = ltSession->addActionListToSession( actionList, tmpFolder, addrList );
    modifyPromise.set_value( hash );

    std::cout << "Client is waiting pauseMutex.unlock" << std::endl;

    // wait the end of replicator's work
    clientPauseMutex.lock();

    fs::remove_all( tmpFolder );
    sleep(10);

    std::cout << "Client finished" << std::endl;
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
        std::vector<uint8_t> data(1024*1024/2);
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


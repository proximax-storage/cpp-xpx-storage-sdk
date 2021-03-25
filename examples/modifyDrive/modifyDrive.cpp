#include "LibTorrentSession.h"
#include "Drive.h"

#include <memory>
#include <string>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <thread>
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
#define DRIVE_ROOT_FOLDER "/Users/alex/111/test_drive_root"

namespace fs = std::filesystem;

using namespace xpx_storage_sdk;

// forward declarations
//
void client( endpoint_list addrList );
void replicator( );
fs::path createClientFiles();

// global variables
//
std::mutex              pauseMutex;
std::promise<InfoHash>  uploadPromise;

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
    pauseMutex.lock();


    // Start replicator
    //
    std::thread replicatorThread( replicator );
    
    // Make the list of replicator addresses
    //
    endpoint_list replicatorsList;
    boost::asio::ip::address e = boost::asio::ip::address::from_string(CLIENT_IP_ADDR);
    replicatorsList.emplace_back( e, 5551 );

    // Client: upload files
    //
    client( replicatorsList );

    replicatorThread.join();

    return 0;
}

//
// client
//
void client( endpoint_list addrList )
{
    std::cout << "Client started" << std::endl;

//    auto ltSession = createDefaultLibTorrentSession( CLIENT_IP_ADDR ":5551" );
    auto ltSession = createDefaultLibTorrentSession( "127.0.0.1:5550", alertHandler );

    fs::path clientFolder = createClientFiles();

    ActionList actionList;
    actionList.push_back( Action::upload( clientFolder / "a.txt", "a.txt" ) );
    actionList.push_back( Action::upload( clientFolder / "folder1" / "b.bin", "folder1/b.bin" ) );
    actionList.push_back( Action::upload( clientFolder / "c.txt", "c.txt" ) );

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
    pauseMutex.lock();

    fs::remove_all( tmpFolder );

    std::cout << "Client finished" << std::endl;
}

//
// replicatorDownloadHandler
//
void replicatorDownloadHandler ( bool success, InfoHash resultRootInfoHash, std::string error )
{
    if ( success ) {
        std::cout << "successfully uploaded" << std::endl;
    }
    else {
        std::cout << "ERROR: " << error << std::endl;
    }

    // unlock client
    pauseMutex.unlock();
}

//
// replicator
//
void replicator()
{
    std::cout << "Replicator started" << std::endl;

    fs::remove_all( DRIVE_ROOT_FOLDER );
    auto drive = createDefaultDrive( CLIENT_IP_ADDR":5551", DRIVE_ROOT_FOLDER, 100*1024*1024, {} );

    std::cout << "Replicator is waiting of infoHash" << std::endl;
    InfoHash infoHash = uploadPromise.get_future().get();

    std::cout << "Replicator received infoHash" << std::endl;
    
    drive->startModifyDrive( infoHash, replicatorDownloadHandler );

    sleep(600);
}

//
// createClientFiles
//
fs::path createClientFiles() {

    // Create empty tmp folder for testing
    //
    auto tmpFolder = fs::temp_directory_path() / "client_files";
    LOG( "createClientFiles:" << tmpFolder );
    fs::remove_all( tmpFolder );
    fs::create_directories( tmpFolder );

    {
        fs::path a_txt = tmpFolder / "a.txt";
        std::ofstream file( a_txt );
        file.write( "a_txt", 5 );
    }
    {
        fs::path b_bin = tmpFolder / "folder1" / "b.bin";
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

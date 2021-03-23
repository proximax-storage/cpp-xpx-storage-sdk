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

namespace fs = std::filesystem;

using namespace xpx_storage_sdk;

// forward declarations
//
void client2( std::promise<InfoHash>, endpoint_list addrList );
void replicator2( std::future<InfoHash> );

// condition variable and auxiliary variables
//
std::mutex pauseMutex;

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

    std::promise<InfoHash> infoHashPromise;
    auto infoHashFuture = infoHashPromise.get_future();

    // Start replicator
    //
    std::thread replicatorThread2( replicator2, std::move(infoHashFuture) );
    
    // wait replicator started
    //pauseMutex.lock();

    // Make the list of replicator addresses
    //
    endpoint_list addrList;
    boost::asio::ip::address e = boost::asio::ip::address::from_string(CLIENT_IP_ADDR);
    addrList.emplace_back( e, 5551 );

    // Start client
    //
    client2( std::move(infoHashPromise), addrList );

    replicatorThread2.join();

    return 0;
}

//
// client2
//
void client2( std::promise<InfoHash> infoHashPromise, endpoint_list addrList )
{
    std::cout << "Client started" << std::endl;

//    auto ltSession = createDefaultLibTorrentSession( CLIENT_IP_ADDR ":5551" );
    auto ltSession = createDefaultLibTorrentSession( "127.0.0.1:5550", alertHandler );

    ActionList actionList;
    actionList.push_back( Action::upload( "/Users/alex/000/a.txt", "folder1/b.txt" ) );

    // Create empty tmp folder for 'modifyDrive data'
    //
    auto tmpFolder = fs::temp_directory_path() / "modify_drive_data";
    fs::remove_all( tmpFolder );
    fs::create_directories( tmpFolder );

    // start file downloading
    InfoHash hash = ltSession->addActionListToSession( actionList, tmpFolder, addrList );
    infoHashPromise.set_value( hash );

//    for( int i=0; i<10; i++)
//    {
//        ltSession->connectPeers( addrList );
//        sleep(1);
//    }

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
        std::cout << "successfully finished" << std::endl;
    }
    else {
        std::cout << "ERROR: " << error << std::endl;
    }

    pauseMutex.unlock();
}

//
// replicator2
//
void replicator2( std::future<InfoHash> infoHashFuture )
{
    std::cout << "Replicator started" << std::endl;

    //auto drive = createDefaultDrive( "1.0.0.127:5550", "/Users/alex/111/test_drive_root", 100*1024*1024, {} );
    auto drive = createDefaultDrive( CLIENT_IP_ADDR":5551", "/Users/alex/111/test_drive_root", 100*1024*1024, {} );
    //auto ltSession = createDefaultLibTorrentSession( CLIENT_IP_ADDR ":5551" );

    std::cout << "Replicator is waiting of infoHash" << std::endl;
    InfoHash infoHash = infoHashFuture.get();

    std::cout << "Replicator received infoHash" << std::endl;
    drive->startModifyDrive( infoHash, replicatorDownloadHandler );

    // Start file downloading
    //
    //ltSession->downloadFile( infoHash, "/Users/alex/222", replicatorDownloadHandler );
    sleep(60);
}


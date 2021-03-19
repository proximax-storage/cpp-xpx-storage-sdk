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

//
// This example shows how 'client' downloads file from 'replicator'.
//

//!!!
// CLIENT_IP_ADDR should be changed to proper address according to your network settings (see ifconfig)
//!!!
#define CLIENT_IP_ADDR "192.168.1.100"

namespace fs = std::filesystem;

using namespace xpx_storage_sdk;

// forward declarations
//void client( endpoint_list replicatorAddresses, InfoHash infoHashOfSomeFile, fs::path destinationFolder );
void client( std::promise<InfoHash> );
void replicator( std::future<InfoHash> );
fs::path createReplicatorFile();

// condition variable and auxiliary variables
std::condition_variable finishCondVar;
std::mutex              finishMutex;
bool                    isFinished = false;

std::mutex finishClientMutex;
std::mutex finishExampleMutex;


// main
int main(int,char**)
{
    finishClientMutex.lock();

    std::promise<InfoHash> modifyPromise;

    std::thread replicatorThread( replicator, modifyPromise.get_future() );

    client( std::move(modifyPromise) );

    replicatorThread.join();

    return 0;
}

void client( std::promise<InfoHash> infoHashPromise )
{
    std::cout << "Client started" << std::endl;

//    auto ltSession = createDefaultLibTorrentSession( CLIENT_IP_ADDR ":5551" );
    auto ltSession = createDefaultLibTorrentSession( "127.0.0.1:5550" );

    ActionList actionList;
    actionList.push_back( Action::upload( "/Users/alex/000/a.txt", "folder1/b.txt" ) );

    // Make the list of replicator addresses
    //
//    endpoint_list addrList;
//    boost::asio::ip::address e = boost::asio::ip::address::from_string("127.0.0.1");
//    addrList.emplace_back( e, 5550 );

    // Create empty tmp folder for 'modifyDrive data'
    //
    auto tmpFolder = fs::temp_directory_path() / "modify_drive_data";
    fs::remove_all( tmpFolder );
    fs::create_directories( tmpFolder );

    // start file downloading
    InfoHash hash = ltSession->addActionListToSession( actionList, tmpFolder );//, addrList );
    infoHashPromise.set_value( hash );

    //ltSession->connectPeers( addrList );

    std::cout << "Client is waiting finishClientMutex.unlock" << std::endl;

    // wait the end of replicator's work
    finishClientMutex.lock();

    fs::remove_all( tmpFolder );

    std::cout << "Client finished" << std::endl;
}

void replicatorDownloadHandler ( bool success, InfoHash resultRootInfoHash, std::string error )
{
    if ( success ) {
        std::cout << "successfully finished" << std::endl;
    }
    else {
        std::cout << "ERROR: " << error << std::endl;
    }

    finishExampleMutex.unlock();
}

void replicator( std::future<InfoHash> infoHashFuture )
{
    std::cout << "Replicator started" << std::endl;

//    //todo
//    endpoint_list addrList;
//    boost::asio::ip::address e = boost::asio::ip::address::from_string(CLIENT_IP_ADDR);
//    addrList.emplace_back( e, 5551 );

//    auto drive = createDefaultDrive( "1.0.0.127:5550", "/Users/alex/111/test_drive_root", 100*1024*1024, addrList );

    //todo
    endpoint_list addrList;
    boost::asio::ip::address e = boost::asio::ip::address::from_string("1.0.0.127");
    addrList.emplace_back( e, 5550 );

    auto drive = createDefaultDrive( CLIENT_IP_ADDR":5551", "/Users/alex/111/test_drive_root", 100*1024*1024, addrList );

    std::cout << "Replicator is waiting of infoHash" << std::endl;
    InfoHash infoHash = infoHashFuture.get();

    std::cout << "Replicator received infoHash" << std::endl;
    drive->startModifyDrive( infoHash, replicatorDownloadHandler );
}

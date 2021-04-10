#include "Session.h"

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
//!!!
#define CLIENT_IP_ADDR "192.168.1.102"
#define REPLICATOR_IP_ADDR "127.0.0.1"



namespace fs = std::filesystem;

using namespace sirius::drive;


// forward declarations
//
void client( endpoint_list replicatorAddresses, InfoHash infoHashOfSomeFile, fs::path destinationFolder );
void replicator( std::promise<InfoHash> );
fs::path createReplicatorFile();


// global variables for synchronization client and replicator
//
std::condition_variable finishCondVar;
std::mutex              finishMutex;
bool                    isFinished = false;

// clientSessionErrorHandler
void clientSessionErrorHandler( libtorrent::alert* alert )
{
    if ( alert->type() == lt::listen_failed_alert::alert_type )
    {
        std::cerr << "client socket error: " << alert->message() << std::endl;
        exit(-1);
    }
}

// replicatorAlertHandler
void replicatorAlertHandler( libtorrent::alert* alert )
{
    if ( alert->type() == lt::listen_failed_alert::alert_type )
    {
        std::cerr << "replicator socket error: "  << alert->message() << std::endl << std::flush;
        exit(-1);
    }
}


//
// main
//
int main(int,char**)
{
    std::promise<InfoHash> infoHashPromise;
    auto infoHashFuture = infoHashPromise.get_future();

    // Start replicator
    //
    std::thread replicatorThread( replicator,std::move(infoHashPromise) );


    // Prepare destination folder, where the file will be saved
    //
    auto dstFolder = fs::temp_directory_path() / "downloader_files";
    fs::remove_all( dstFolder );
    fs::create_directories( dstFolder );

    // Make the list of replicator addresses
    //
    endpoint_list addrList;
    boost::asio::ip::address e = boost::asio::ip::address::from_string(REPLICATOR_IP_ADDR);
    addrList.emplace_back( e, 5550 );

    // Receive InfoHash of file to be downloaded
    InfoHash infoHash = infoHashFuture.get();

    // Run client
    //
    client( addrList, infoHash, dstFolder );

    // Print the result
    //
    if ( isFinished ) {
        std::cout << "successfuly completed" << std::endl;
    }
    else {
        std::cout << "download failed" << std::endl;
    }

    // Wait thread
    //
    replicatorThread.join();

    return 0;
}

//
// progressHandler
//
void progressHandler( download_status::code code, InfoHash, std::string )
{
    if ( code == download_status::complete ) {
        isFinished = true;
        finishCondVar.notify_all();
    }
    else if ( code == download_status::failed )
    {
        isFinished = false;
        finishCondVar.notify_all();
    }
}

//
// client
//
void client( endpoint_list replicatorAddresses, InfoHash infoHashOfSomeFile, fs::path destinationFolder )
{
    // Create libtorrent session
    //
    auto ltSession = createDefaultSession( CLIENT_IP_ADDR ":5551", clientSessionErrorHandler );

    // Start file downloading
    //
    ltSession->downloadFile( infoHashOfSomeFile,
                             destinationFolder,
                             progressHandler,
                             replicatorAddresses );

    // Wait for the download to finish
    //
    std::unique_lock<std::mutex> lock(finishMutex);
    finishCondVar.wait( lock, []{return isFinished;} );
}

//
// replicator
//
void replicator( std::promise<InfoHash> infoHashPromise )
{
    fs::path file = createReplicatorFile();

    // Create torrent file
    //
    fs::path torrentFile = file.parent_path() / "info.torrent";
    InfoHash infoHashOfFile = createTorrentFile( file, file.parent_path(), torrentFile );

    // Emulate replicator side
    auto ltSession = createDefaultSession( REPLICATOR_IP_ADDR ":5550", replicatorAlertHandler );
    ltSession->addTorrentFileToSession( torrentFile, file.parent_path() );

    // Pass InfoHash
    infoHashPromise.set_value( infoHashOfFile );

    // Wait for the download to finish
    std::unique_lock<std::mutex> lock(finishMutex);
    finishCondVar.wait( lock, []{return isFinished;} );
}

//
// createReplicatorFile
//
fs::path createReplicatorFile() {

    // Create empty tmp folder for testing
    //
    auto tmpFolder = fs::temp_directory_path() / "replicator_files";
    fs::remove_all( tmpFolder );
    fs::create_directories( tmpFolder );

    // Create file.bin
    //
    fs::path fname = tmpFolder / "file.bin";

    std::vector<uint8_t> data(1024*1024/2);
    std::generate( data.begin(), data.end(), std::rand );

    std::ofstream file( fname );
    file.write( (char*) data.data(), data.size() );

    // Return path to file
    return fname;
}



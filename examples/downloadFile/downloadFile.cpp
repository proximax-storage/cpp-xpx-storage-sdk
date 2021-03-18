#include "LibTorrentSession.h"

#include <memory>
#include <string>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <thread>
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
void client( endpoint_list replicatorAddresses, InfoHash infoHashOfSomeFile, fs::path destinationFolder );
void replicator( InfoHash& outInfoHashOfSomeFile );
fs::path createReplicatorFile();

// condition variable and auxiliary variables
std::condition_variable finishCondVar;
std::mutex              finishMutex;
bool                    isFinished = false;

// main
int main(int,char**)
{
    // start replicator and receive InfoHash of some file
    InfoHash hashOfSomeFile;
    std::thread replicatorThread( [&hashOfSomeFile] { replicator( hashOfSomeFile ); });

    // wait 'replicator is ready'
    sleep(3);

    // prepare destination folder, where the file will be saved
    auto dstFolder = fs::temp_directory_path() / "downloader_files";
    fs::remove_all( dstFolder );
    fs::create_directories( dstFolder );

    // make the list of replicator addresses
    endpoint_list addrList;
    boost::asio::ip::address e = boost::asio::ip::address::from_string("127.0.0.1");
    addrList.emplace_back( e, 5550 );

    // run client
    client( addrList, hashOfSomeFile, dstFolder );

    // print the result
    if ( isFinished ) {
        std::cout << "successfuly completed" << std::endl;
    }
    else {
        std::cout << "download failed" << std::endl;
    }

    // wait thread
    replicatorThread.join();

    return 0;
}

// progressHandler
void progressHandler( download_status::code code, InfoHash, const std::string& info )
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

// client
void client( endpoint_list replicatorAddresses, InfoHash infoHashOfSomeFile, fs::path destinationFolder )
{
    // create libtorrent session
    auto ltWrapper = createDefaultLibTorrentSession( CLIENT_IP_ADDR ":5551" );

    // start file downloading
    ltWrapper->downloadFile( infoHashOfSomeFile,
                             destinationFolder,
                             progressHandler,
                             replicatorAddresses );

    // wait for the download to finish
    std::unique_lock<std::mutex> lock(finishMutex);
    finishCondVar.wait( lock, []{return isFinished;} );
}

// replicator
void replicator( InfoHash& outInfoHashOfSomeFile )
{
    fs::path file = createReplicatorFile();

    // Create torrent file
    fs::path torrentFile = file.parent_path() / "info.torrent";
    outInfoHashOfSomeFile = createTorrentFile( file, torrentFile );

    // emulate replicator side
    auto ltWrapper = createDefaultLibTorrentSession("127.0.0.1:5550");
    ltWrapper->addTorrentFileToSession( torrentFile, file );

    // wait for the download to finish
    std::unique_lock<std::mutex> lock(finishMutex);
    finishCondVar.wait( lock, []{return isFinished;} );
}

// createReplicatorFile
fs::path createReplicatorFile() {

    // create empty tmp folder for testing
    auto tmpFolder = fs::temp_directory_path() / "replicator_files";
    fs::remove_all( tmpFolder );
    fs::create_directories( tmpFolder );

    // create file.bin
    fs::path fname = tmpFolder / "file.bin";

    std::vector<uint8_t> data(1024*1024/2);
    std::generate( data.begin(), data.end(), std::rand );

    std::ofstream file( fname );
    file.write( (char*) data.data(), data.size() );

    return fname;
}



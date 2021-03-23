#include "LibTorrentSession.h"

#include <memory>
#include <string>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <condition_variable>

#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>

//
// This example shows how 'file-povider' and 'downloader' work.
//
// IP_ADDR_2 should be changed to the address according to your network settings (see ifconfig)
//

#define IP_ADDR_2 "192.168.1.100"

namespace fs = std::filesystem;

using namespace xpx_storage_sdk;

void     downloader( InfoHash infoHash );
fs::path createProviderFolderWithFiles();

std::condition_variable finishCondVar;
std::mutex              finishMutex;
bool                    isFinished = false;

// alertHandler
void alertHandler( LibTorrentSession*, libtorrent::alert* alert )
{
    std::cout << "alert: " << alert->message() << std::endl;

    if ( alert->type() == lt::listen_failed_alert::alert_type )
    {
        exit(-1);
    }
}

// progressHandler
void progressHandler( download_status::code code, InfoHash, const std::string& info ) {
    std::cout << "progressHandler" << std::endl;
    assert( code == download_status::complete );
    isFinished = true;
    finishCondVar.notify_all();
}

// main
int main(int,char**) {

    auto tmpFolder = createProviderFolderWithFiles();

    // Create torrent file for tmp folder
    InfoHash infoHash = createTorrentFile( tmpFolder.string(), tmpFolder.replace_extension("torrent").string() );

    // Run file provider
    auto ltWrapper = createDefaultLibTorrentSession( "127.0.0.1:5550", alertHandler );
    ltWrapper->addTorrentFileToSession( tmpFolder.replace_extension("torrent").string(), tmpFolder.string() );
    
    downloader( infoHash );

    std::unique_lock<std::mutex> lock(finishMutex);
    finishCondVar.wait( lock, []{return isFinished;} );

    std::cout << "successfuly completed" << std::endl;

    return 0;
}

// downloader
void downloader( InfoHash infoHash ) {

    auto rcvFolder = fs::temp_directory_path() / "receiver";
    fs::remove_all( rcvFolder );
    fs::create_directories( rcvFolder );
    std::cout << "rcv: " << rcvFolder << std::endl;

    auto ltWrapper = createDefaultLibTorrentSession( IP_ADDR_2 ":5551", alertHandler );

    endpoint_list eList;
    boost::asio::ip::address e = boost::asio::ip::address::from_string("127.0.0.1");
    eList.emplace_back( e, 5550 );
    ltWrapper->downloadFile( infoHash, rcvFolder.string(), progressHandler, eList);

    std::unique_lock<std::mutex> lock(finishMutex);
    finishCondVar.wait( lock, []{return isFinished;} );
}

// createProviderFolderWithFiles
fs::path createProviderFolderWithFiles() {

    // create empty tmp folder for testing
    auto tmpFolder = fs::temp_directory_path() / "lt_test_tmpFolder" / "subFolder";
    fs::remove_all( tmpFolder );
    fs::create_directories( tmpFolder );

    //
    // create file1.bin
    //
    auto fname1 = tmpFolder / "file1.bin";
    std::cout << "fname1: " << fname1 << std::endl;

    std::vector<uint8_t> data1(1024*1024/2);
    //std::srand(unsigned(std::time(nullptr)));
    std::generate( data1.begin(), data1.end(), std::rand );

    std::ofstream file1( fname1 );
    file1.write( (char*) data1.data(), data1.size() );

    //
    // create file2.bin
    //
    std::string fname2 = tmpFolder / "file2.bin";
    std::cout << "fname2: " << fname2 << std::endl;

    std::vector<uint8_t> data2(10*1024*1024);
    //std::srand(unsigned(std::time(nullptr)));
    std::generate( data2.begin(), data2.end(), std::rand );

    std::ofstream file2( fname2 );
    file2.write( (char*) data2.data(), data2.size() );

    return tmpFolder;
}



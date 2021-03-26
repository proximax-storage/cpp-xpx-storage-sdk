#include "drive/LibTorrentSession.h"

#include <string>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <condition_variable>

//
// This example shows how 'file-povider' and 'downloader' work.
//
// IP_ADDR_2 should be changed to the address according to your network settings (see ifconfig)
//

#define IP_ADDR_2 "192.168.1.100"

void     downloader(sirius::Hash256 infoHash);
std::filesystem::path createProviderFolderWithFiles();

std::condition_variable condVariable;
std::mutex              cvMutex;
bool                    downloadCompleted = false;

// progressHandler
void progressHandler(sirius::download_status::code code, sirius::Hash256, const std::string&) {
    assert(code == sirius::download_status::complete);
    downloadCompleted = true;
    condVariable.notify_all();
}

// main
int main(int,char**) {

    auto tmpFolder = createProviderFolderWithFiles();

    // Create torrent file for tmp folder
    sirius::Hash256 infoHash = sirius::drive::createTorrentFile(tmpFolder.string(), tmpFolder.replace_extension("torrent").string());

    // Run file provider
    auto ltWrapper = sirius::drive::createDefaultLibTorrentWrapper("127.0.0.1:5550");
    ltWrapper->addTorrentFileToSession(tmpFolder.replace_extension("torrent").string(), tmpFolder.string());
    
    downloader(infoHash);

    std::unique_lock<std::mutex> lock(cvMutex);
    condVariable.wait(lock, []{return downloadCompleted;});

    std::cout << "successfuly completed" << std::endl;

    return 0;
}

// downloader
void downloader(sirius::Hash256 infoHash) {

    auto rcvFolder = std::filesystem::temp_directory_path() / "receiver";
    std::filesystem::remove_all(rcvFolder);
    std::filesystem::create_directories(rcvFolder);
    std::cout << "rcv: " << rcvFolder << std::endl;

    auto ltWrapper = sirius::drive::createDefaultLibTorrentWrapper(IP_ADDR_2 ":5551");

    endpoint_list eList;
    boost::asio::ip::address e = boost::asio::ip::address::from_string("127.0.0.1");
    eList.emplace_back(e, 5550);
    ltWrapper->downloadFile(infoHash, rcvFolder.string(), progressHandler, eList);

    std::unique_lock<std::mutex> lock(cvMutex);
    condVariable.wait(lock, []{return downloadCompleted;});
}

// createProviderFolderWithFiles
std::filesystem::path createProviderFolderWithFiles() {

    // create empty tmp folder for testing
    auto tmpFolder = std::filesystem::temp_directory_path() / "lt_test_tmpFolder" / "subFolder";
    std::filesystem::remove_all(tmpFolder);
    std::filesystem::create_directories(tmpFolder);

    //
    // create file1.bin
    //
    auto fname1 = tmpFolder / "file1.bin";
    std::cout << "fname1: " << fname1 << std::endl;

    std::vector<uint8_t> data1(1024*1024/2);
    //std::srand(unsigned(std::time(nullptr)));
    std::generate(data1.begin(), data1.end(), std::rand);

    std::ofstream file1(fname1);
    file1.write((char*) data1.data(), data1.size());

    //
    // create file2.bin
    //
    std::string fname2 = tmpFolder / "file2.bin";
    std::cout << "fname2: " << fname2 << std::endl;

    std::vector<uint8_t> data2(1*1024*1024);
    //std::srand(unsigned(std::time(nullptr)));
    std::generate(data2.begin(), data2.end(), std::rand);

    std::ofstream file2(fname2);
    file2.write((char*) data2.data(), data2.size());

    return tmpFolder;
}



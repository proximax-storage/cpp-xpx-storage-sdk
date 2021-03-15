#include <LibTorrentWrapper.h>

#include <memory>
#include <string>

using namespace xpx_storage_sdk;

int main(int argc, char *argv[]) {

    //InfoHash infoHash = createTorrentFile( std::string pathToFolderOrFolder, std::string outputTorrentFilename = "" );

    auto ltWrapper = createDefaultLibTorrentWrapper();

    //ltWrapper->addTorrentFileToSession();

    return 0;
}

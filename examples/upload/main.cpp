#include <FileTransmitter.h>

#include <memory>
#include <string>

using namespace xpx_storage_sdk;

int main(int argc, char *argv[]) {
    std::shared_ptr<FileTransmitter> ft = createDefaultFileTransmitter();
    ft->init("0.0.0.0:5551");

    std::string path = "./files/bc.log";

    Key driveKey;

    ft->addFile(driveKey, path, [](error::code code, const std::string& textMessage) {});

    // wait for the user to end
    char a;
    int ret = std::scanf("%c\n", &a);
    (void) ret; // ignore
    return 0;
}

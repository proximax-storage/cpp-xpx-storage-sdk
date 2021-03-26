#include "drive/FileTransmitter.h"
#include <memory>
#include <string>

int main(int, char *[]) {
    std::shared_ptr<sirius::drive::FileTransmitter> ft = sirius::drive::CreateDefaultFileTransmitter();
    ft->init("0.0.0.0:5551");

    std::string path = "./files/bc.log";

	sirius::Key driveKey;

    ft->addFile(driveKey, path, [](sirius::error::code, const std::string&) {});

    // wait for the user to end
    char a;
    int ret = std::scanf("%c\n", &a);
    (void) ret; // ignore
    return 0;
}

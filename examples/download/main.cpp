#include "drive/FileTransmitter.h"
#include <memory>
#include <string>
#include <iostream>

int main(int, char *[]) {
    std::shared_ptr<sirius::drive::FileTransmitter> ft = sirius::drive::CreateDefaultFileTransmitter();
    ft->init("192.168.1.100:5550");

    std::string hash = "7098b8d0f216ba7ac4dd7afc21fe5e486b0f1396faf205df8c7fe51363b10177";
    std::string path = "./downloads";

    std::cout << hash << std::endl;

	sirius::Hash256 finalHash;

    std::copy(hash.begin(), hash.end(), std::begin(finalHash));

    ft->download(finalHash, path, [](sirius::download_status::code code, sirius::Hash256 h, const std::string &fileName) {
        std::cout << "code: " << code << std::endl;
        std::cout << "hash: " << h.data() << std::endl;
        std::cout << "fileName: " << fileName << std::endl;
        exit(0);
    }, "127.0.0.1", 5551);

    // wait for the user to end
    char a;
    int ret = std::scanf("%c\n", &a);
    (void) ret; // ignore
    return 0;
}

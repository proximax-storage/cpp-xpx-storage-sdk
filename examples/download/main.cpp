#include <FileTransmitter.h>

#include <memory>
#include <string>
#include <iostream>

using namespace xpx_storage_sdk;

int main(int argc, char *argv[]) {
    std::shared_ptr<FileTransmitter> ft = createDefaultFileTransmitter();
    ft->init("10.0.0.14:6800");

    std::string hash = "fa63b7c86a9b6086d9640246fbf3a1dd127db8dc01065976a085bdde25f0f101";
    //std::string hash = "0b62316438efa6d1970b5c150bbed3f57a010289f4dd4901c8c94bbbbf7aa601";
    std::string path = "./downloads";

    FileHash finalHash;

    std::copy(hash.begin(), hash.end(), std::begin(finalHash));

    ft->download(finalHash, path, [](download_status::code code, FileHash h, const std::string &fileName) {
        std::cout << "code: " << code << std::endl;
        std::cout << "hash: " << h.data() << std::endl;
        std::cout << "fileName: " << fileName << std::endl;
        exit(0);
    }, "10.0.0.14", 6881);

    // wait for the user to end
    char a;
    int ret = std::scanf("%c\n", &a);
    (void) ret; // ignore
    return 0;
}
/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "FileTransmitter.h"

//#include <libtorrent?>

namespace xpx_storage_sdk {

//DefaultFileTransmitter
class DefaultFileTransmitter : public FileTransmitter {
public:
    DefaultFileTransmitter() {}

    virtual ~DefaultFileTransmitter() {}

    FileHash prepareActionListToUpload( const ActionList&, std::string addr, int port) override {

        return FileHash();
    }

    void download( FileHash, std::string outputFolder, DownloadFileHandler, std::string addr = "", int port = 0 )  override {
    }

    void addFile( Key drivePubKey, FileHash, std::string fileNameWithPath, ErrorHandler )  override {

    }

    void removeFile( Key drivePubKey, FileHash, std::string fileNameWithPath, ErrorHandler )  override {

    }

};

std::shared_ptr<FileTransmitter> createDefaultFileTransmitter() {

    return std::make_shared<DefaultFileTransmitter>();
}


}

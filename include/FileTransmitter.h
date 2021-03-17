/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "types.h"
#include "ActionList.h"
#include <memory>

namespace xpx_storage_sdk {

    // FileTransmitter
    class FileTransmitter {
    public:

        virtual ~FileTransmitter() = default;

        virtual void init(const std::string& address = "0.0.0.0:6881") = 0;

        virtual FileHash prepareActionListToUpload( const ActionList&, std::string addr = "", int port = 0 ) = 0;

        virtual void download( FileHash, const std::string& outputFolder, DownloadHandler, const std::string& address = "", unsigned short port = 0 ) = 0;

        // Replicator functionality only
        virtual void addFile( Key drivePubKey, std::string fileNameWithPath, ErrorHandler ) = 0;
        virtual void removeFile( Key drivePubKey, FileHash, std::string fileNameWithPath, ErrorHandler ) = 0;


        //virtual void monitorUploadStatus( FileHash actionListHash, UploadHandler );
    };

    std::shared_ptr<FileTransmitter> createDefaultFileTransmitter();
};

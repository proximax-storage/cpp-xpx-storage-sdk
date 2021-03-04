/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "types.h"
#include "ActionList.h"

namespace xpx_storage_sdk {

    // FileTransmitter
    class FileTransmitter {

        virtual ~FileTransmitter() = default;

        virtual FileHash prepareActionListToUpload( const ActionList&, std::string addr = "", int port = 0 );

        virtual void downloadFile( FileHash, std::string outputFolder, DownloadFileHandler, std::string addr = "", int port = 0 ) = 0;

        // Replicator functionality only
        virtual void addAvailableFile( Key drivePubKey, FileHash, std::string fileNameWithPath, ErrorHandler ) = 0;
        virtual void removeAvailableFile( Key drivePubKey, FileHash, std::string fileNameWithPath, ErrorHandler ) = 0;


        //virtual void monitorUploadStatus( FileHash actionListHash, UploadHandler );
    };

    std::shared_ptr<FileTransmitter> createDefaultFileTransmitter();
};

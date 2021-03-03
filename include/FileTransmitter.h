/*
*** Copyright 2019 ProximaX Limited. All rights reserved.
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

        virtual void connectToPeer( std::string addr, int port, ErrorHandler ) = 0;
        virtual void downloadFile( Key drivePubKey, FileHash, DownloadFileHandler ) = 0;

        virtual FileHash prepareActionListToUpload( const ActionList& );

        virtual void monitorUploading( FileHash actionListHash, UploadHandler );
    };

    std::shared_ptr<FileTransmitter> createDefaultFileTransmitter();
};

/*
*** Copyright 2019 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "XpxStorageSdk.h"
#include "ActionList.h"

namespace xpx_storage_sdk {

    class IStorageClient { //: public std::enable_shared_from_this<IStorageClient> {
    public:
        virtual ~IStorageClient() {} // = default;

        virtual DriveHash generateNewDriveHash() = 0;

        virtual void createDrive( DriveHash hash, size_t size, ErrorHandler func ) = 0;
        virtual void closeDrive( DriveHash hash, ErrorHandler func ) = 0;

        virtual void sendToBlockchain( ActionList&, UploadHandler func ) = 0;

        // downloadDriveStruct will be defined later
        //virtual void downloadDriveStruct(  ) = 0;

    };

    std::shared_ptr<IStorageClient> createStorageClient();
};

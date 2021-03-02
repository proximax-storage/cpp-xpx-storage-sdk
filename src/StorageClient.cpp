/*
*** Copyright 2019 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "StorageClient.h"

namespace xpx_storage_sdk {

// StorageClient
class StorageClient: public IStorageClient {
public:
    StorageClient() {}

    ~StorageClient() {}

    DriveHash generateNewDriveHash() {
        return DriveHash();
    }

    void createDrive( DriveHash hash, size_t size, ErrorHandler func ) override
    {
        func( error::success, "" );
    }

    void closeDrive( DriveHash hash, ErrorHandler func )  override
    {
        func( error::success, "" );
    }

    virtual void sendToBlockchain( ActionList&, UploadHandler func )  override
    {
        func( upload_status::complete, "" );
    }

};

// createStorageClient
std::shared_ptr<IStorageClient> createStorageClient() {
    return std::make_shared<StorageClient>();
}


}

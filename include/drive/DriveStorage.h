/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once
#include "types.h"
#include "Drive.h"

namespace sirius { namespace drive {

    struct DriveInfo {
        Key     PubKey;
        size_t  Size;
        size_t  FreeSize;
    };

    // DriveStorage
    class DriveStorage {

        virtual ~DriveStorage() = default;
        
        virtual void init(const std::string& rootPath, size_t maxStorageSize) = 0;

        virtual void createDrive(const Key& drivePubKey, size_t size) = 0;
        virtual void closeDrive(const Key& drivePubKey) = 0;
        virtual DriveInfo getDriveInfo(const Key& drivePubKey) = 0;
        
        //virtual std::vector<Drive> getDrives() = 0;
    };

    std::shared_ptr<DriveStorage> CreateDefaultDriveStorage();
}}

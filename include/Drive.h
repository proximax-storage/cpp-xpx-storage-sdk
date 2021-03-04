/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "types.h"
#include "Drive.h"

namespace xpx_storage_sdk {

    // Drive
    class DriveStorage {

        virtual ~DriveStorage() = default;
        
        virtual void init( std::string rootPath, size_t maxDriveSize ) = 0;

//        virtual void createDrive( Key drivePubKey, size_t size ) = 0;
//        virtual void closeDrive( Key drivePubKey ) = 0;
//        virtual DriveInfo getDriveInfo( Key drivePubKey ) = 0;
    };

    std::shared_ptr<DriveStorage> createDefaultDriveStorage();
};

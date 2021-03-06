/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "Drive.h"

using namespace xpx_storage_sdk;

int main() {

    auto drive = createDefaultDrive( "~/000" );

    DriveTree driveStruct;

    drive->createDriveStruct( driveStruct, "/Users/alex/111" );

    return 0;
}

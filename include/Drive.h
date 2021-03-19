/*#include <memory>
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "types.h"
#include "FsTree.h"
#include <memory>

namespace xpx_storage_sdk {

    using ModifyDriveResultHandler = std::function<void( bool success, InfoHash resultRootInfoHash, std::string error )>;

    // Drive
    class Drive {
    public:

        virtual ~Drive() = default;

        virtual void startModifyDrive( InfoHash modifyDataInfoHash, ModifyDriveResultHandler ) = 0;

        //virtual void executeActionList( InfoHash actionListHash ) = 0;


        //todo
        //virtual bool createDriveStruct( FsTree& node, const std::string& path, const std::string& logicalPath = "" ) = 0;
    };

    std::shared_ptr<Drive> createDefaultDrive( std::string listenInterface,
                                               std::string rootPath,
                                               size_t maxSize,
                                               endpoint_list otherReplicators = {}
                                               );
};

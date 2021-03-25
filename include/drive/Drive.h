/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once
#include "types.h"
#include "drive/FsTree.h"
#include <memory>

namespace sirius { namespace drive { class FileTransmitter; }}

namespace sirius { namespace drive {

    class Drive {
    public:
        virtual ~Drive() = default;

	public:
        virtual void init(const Key& drivePubKey, size_t maxDriveSize, std::shared_ptr<FileTransmitter> pFileTransmitter) = 0;
        virtual void executeActionList(const Hash256& actionListHash) = 0;
        virtual bool createDriveStruct(const FsTree& node, const std::string& path, const std::string& logicalPath = "") = 0;

    };

    std::shared_ptr<Drive> СreateDefaultDrive(std::string rootPath);
}}

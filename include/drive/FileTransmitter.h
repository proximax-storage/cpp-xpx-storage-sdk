/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once
#include "types.h"
#include "ActionList.h"
#include <memory>

namespace sirius { namespace drive {

    // FileTransmitter
    class FileTransmitter {
    public:
        virtual ~FileTransmitter() = default;

	public:
        virtual void init(const std::string& address = "0.0.0.0:6881") = 0;

        virtual Hash256 prepareActionListForUpload(const ActionList&, const std::string& addr = "", uint16_t port = 0) = 0;

        virtual void download(
        	const Hash256& hash,
        	const std::string& outputFolder,
        	DownloadHandler handler,
        	const std::string& address = "",
        	uint16_t port = 0) = 0;

        // Replicator functionality only
        virtual void addFile(const Key& drivePubKey, const std::string& filePath, ErrorHandler handler) = 0;
        virtual void removeFile(const Key& drivePubKey, const Hash256&, const std::string& filePath, ErrorHandler handler) = 0;


        //virtual void monitorUploadStatus(Hash256 actionListHash, UploadHandler);
    };

    std::shared_ptr<FileTransmitter> CreateDefaultFileTransmitter();
}}

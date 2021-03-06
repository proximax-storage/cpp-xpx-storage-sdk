/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "Drive.h"
#include "FileTransmitter.h"
#include <filesystem>
#include <iostream>


namespace xpx_storage_sdk {
using namespace fs_tree;

class DefaultDrive: public Drive {
    std::string m_rootPath;
    Key         m_drivePubKey;

    std::shared_ptr<FileTransmitter> m_fileTransmitter;

public:

    DefaultDrive( std::string rootPath ) : m_rootPath(rootPath) {}

    virtual ~DefaultDrive() {}

    void init( Key      drivePubKey,
               size_t   maxDriveSize,
               std::shared_ptr<FileTransmitter> fileTransmitter ) override
    {
        m_drivePubKey = drivePubKey;
        m_fileTransmitter = fileTransmitter;

        //TODO load drive structure


        //TODO load files to libtorrent

    }

    void executeActionList( FileHash actionListHash ) override {

    }


    bool createDriveStruct( FsTree& node, const std::string& path, const std::string& logicalPath )
    {
        return true;
    }

};

std::shared_ptr<Drive> createDefaultDrive( std::string rootPath ) {
    return std::make_shared<DefaultDrive>( rootPath );
}

}

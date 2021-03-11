/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "types.h"
#include "ActionList.h"
#include <memory>

#include "types.h"

namespace xpx_storage_sdk {

    // LibTorrentWrapper
    class LibTorrentWrapper {
    public:

        void createSession( const std::string& address = "0.0.0.0:6881" );
        void deleteSession();

        // createTorrentFileFor - returns root hash
        std::string createTorrentFile( std::string pathToFilerOrFolder, std::string outputTorrentFilename );

        bool    addTorrentFileToSession( std::string torrentFilename );
        bool    pause(); //?????
        bool    resume(); //?????

        void downloadFile( std::string fileHash, std::string outputFolder, DownloadHandler );
    };

};

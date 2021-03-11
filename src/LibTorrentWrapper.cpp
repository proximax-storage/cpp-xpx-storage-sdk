/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "LibTorrentWrapper.h"

namespace xpx_storage_sdk {

    void LibTorrentWrapper::createSession( const std::string& address ) {
    }

    void LibTorrentWrapper::deleteSession() {
    }

    std::string LibTorrentWrapper::createTorrentFile( std::string pathToFilerOrFolder, std::string outputTorrentFilename ) {
        return "0000000000000000000000000000000000000000000000000000000000000000";
    }

    bool LibTorrentWrapper::addTorrentFileToSession( std::string torrentFilename, std::string peerAddrWithPort ) {
    }

    void LibTorrentWrapper::downloadFile( std::string fileHash, std::string outputFolder, DownloadHandler, std::string peerAddrWithPort ) {

    };

};

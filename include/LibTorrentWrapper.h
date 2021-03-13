/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "types.h"
#include "ActionList.h"
#include <vector>
#include <boost/asio/ip/tcp.hpp>

#include "types.h"

using  tcp = boost::asio::ip::tcp;
using  endpoint_list = std::vector<tcp::endpoint>;

namespace xpx_storage_sdk {

// LibTorrentWrapper
class LibTorrentWrapper {
public:

    virtual ~LibTorrentWrapper() = default;

    virtual void     createSession() = 0;
    virtual void     deleteSession() = 0;

    virtual bool     addTorrentFileToSession( std::string torrentFilename,
                                              std::string fileFolder,
                                              endpoint_list = {} ) = 0;

    virtual InfoHash addActionListToSession( const ActionList&,
                                             const std::string& tmpFolderPath,
                                             endpoint_list list = {} ) = 0;

    virtual void     downloadFile( InfoHash,
                                   std::string outputFolder,
                                   DownloadHandler,
                                   endpoint_list list = {} ) = 0;
};

InfoHash createTorrentFile( std::string pathToFolderOrFolder, std::string outputTorrentFilename = "" );

std::shared_ptr<LibTorrentWrapper> createDefaultLibTorrentWrapper( std::string address = "0.0.0.0:6881" );

};

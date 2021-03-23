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

namespace libtorrent {
    struct alert;
}

namespace xpx_storage_sdk {

// LibTorrentSession
class LibTorrentSession {
public:

    virtual ~LibTorrentSession() = default;

    virtual void     endSession() = 0;

    virtual void     addTorrentFileToSession( std::string torrentFilename,
                                              std::string rootFolder,
                                              endpoint_list = {} ) = 0;

    virtual InfoHash addActionListToSession( const ActionList&,
                                             const std::string& workFolder,
                                             endpoint_list list = {} ) = 0;

    virtual void     downloadFile( InfoHash,
                                   std::string outputFolder,
                                   DownloadHandler,
                                   endpoint_list list = {} ) = 0;

    virtual void     connectPeers( endpoint_list list ) = 0;
};

// createTorrentFile
InfoHash createTorrentFile( std::string pathToFolderOrFolder, std::string outputTorrentFilename = "" );


//
// createDefaultLibTorrentSession
//

using LibTorrentAlertHandler = std::function<void( LibTorrentSession*, libtorrent::alert* )>;

std::shared_ptr<LibTorrentSession> createDefaultLibTorrentSession( std::string address, LibTorrentAlertHandler );

};

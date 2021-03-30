/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "types.h"
#include "ActionList.h"
#include <libtorrent/torrent_handle.hpp>
#include <vector>
#include <boost/asio/ip/tcp.hpp>

#include "types.h"

using  tcp = boost::asio::ip::tcp;

namespace libtorrent {
    struct alert;
}

namespace sirius { namespace drive {

#define FS_TREE_FILE_NAME "FsTree.bin"

//
namespace download_status {
    enum code {
        complete = 0,
        uploading = 2,
        failed = 3
    };
};

//
using DownloadHandler = std::function<void( download_status::code code, InfoHash, const std::string& info )>;

//
class LibTorrentSession {
public:
    using lt_handle = lt::torrent_handle;
    using RemoveHandler = std::optional<std::function<void()>>;

    virtual ~LibTorrentSession() = default;

    virtual void      endSession() = 0;

    virtual lt_handle addTorrentFileToSession( std::string torrentFilename,
                                               std::string savePath,
                                               endpoint_list = {} ) = 0;

    virtual InfoHash  addActionListToSession( const ActionList&,
                                              const std::string& workFolder,
                                              endpoint_list list = {} ) = 0;

    virtual void      downloadFile( InfoHash,
                                    std::string outputFolder,
                                    DownloadHandler,
                                    endpoint_list list = {} ) = 0;

    virtual void      removeTorrentFromSession( lt_handle, RemoveHandler h = {} ) = 0;

};

// createTorrentFile
InfoHash createTorrentFile( std::string pathToFolderOrFolder, std::string pathToRootFolder, std::string outputTorrentFilename );

//
// createDefaultLibTorrentSession
//

using LibTorrentAlertHandler = std::function<void( LibTorrentSession*, libtorrent::alert* )>;

std::shared_ptr<LibTorrentSession> createDefaultLibTorrentSession( std::string address, LibTorrentAlertHandler );

}}

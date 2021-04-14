/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "types.h"
#include "ActionList.h"
#include <libtorrent/torrent_handle.hpp>
#include <boost/asio/ip/tcp.hpp>

using  tcp = boost::asio::ip::tcp;
using  endpoint_list = std::vector<boost::asio::ip::tcp::endpoint>;


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
class Session {
public:
    using lt_handle = lt::torrent_handle;
    using RemoveHandler = std::optional<std::function<void()>>;

    virtual ~Session() = default;

    virtual void      endSession() = 0;

    virtual lt_handle addTorrentFileToSession( const std::string& torrentFilename,
                                               const std::string& savePath,
                                               endpoint_list = {} ) = 0;

    virtual void      removeTorrentFromSession( lt_handle, RemoveHandler h = {} ) = 0;

    virtual InfoHash  addActionListToSession( const ActionList&,
                                              const std::string& workFolder,
                                              endpoint_list list = {} ) = 0;

    virtual void      downloadFile( InfoHash,
                                    const std::string& outputFolder,
                                    DownloadHandler,
                                    endpoint_list list = {} ) = 0;

};

// createTorrentFile
InfoHash createTorrentFile( const std::string& pathToFolderOrFolder,
                            const std::string& /*pathToRootFolder*/,
                            const std::string& outputTorrentFilename );

// calculateRootHash
RootHash calculateRootHash( const std::string& pathToFile );

// calculateInfoHash (InfoHash is a part of magnetlink)
//InfoHash calculateInfoHash( const std::string& pathToFile, const std::string& drivePublicKey );

// calculateInfoHashAndTorrent (InfoHash is a part of magnetlink)
InfoHash calculateInfoHashAndTorrent( const std::string& pathToFile,
                                      const std::string& drivePublicKey,
                                      const std::string& outputTorrentPath );

//
// createDefaultLibTorrentSession
//

using LibTorrentErrorHandler = std::function<void( libtorrent::alert* )>;

std::shared_ptr<Session> createDefaultSession( std::string address, const LibTorrentErrorHandler& );

}}

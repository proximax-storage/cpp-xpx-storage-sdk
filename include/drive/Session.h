/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "types.h"
#include "ActionList.h"

#include <filesystem>
#include <libtorrent/torrent_handle.hpp>
#include <boost/asio/ip/tcp.hpp>

using  tcp = boost::asio::ip::tcp;
using  endpoint_list = std::vector<boost::asio::ip::tcp::endpoint>;

namespace sirius { namespace drive {

#define FS_TREE_FILE_NAME "FsTree.bin"

struct DownloadContext;

// It will be used to inform 'client' about download status
//
namespace download_status {
    enum code {
        complete = 0,
        uploading = 2,
        failed = 3
    };
};

// It will be used to inform 'client' about download status
//
using DownloadNotification = std::function<void( const DownloadContext&,
                                                 download_status::code,
                                                 const std::string& errorText )>;

// It will be used to inform 'client' about download status
//
struct DownloadContext {

    DownloadContext( DownloadNotification  downloadNotification,
                     InfoHash              infoHash
                     std::filesystem::path saveFolder,
                     std::filesystem::path renameAs = {} )
        :
          m_downloadNotification(downloadNotification),
          m_infoHash(infoHash),
          m_saveFolder(saveFolder),
          m_renameAs(renameAs)
        {}

    DownloadNotification  m_downloadNotification;

    InfoHash              m_infoHash;
    std::filesystem::path m_saveFolder;
    std::filesystem::path m_renameAs;

    float                 m_downloadPercents = 0.;
    // ... todo
};

//
// It will be used to inform 'client' that all required torrents
// have been sucessfully removed from the session.
// And only after that 'client' could remove/move files and torrnet file.
//
struct RemoveTorrentContext
{
    using  RemoveNotification = std::optional<std::function<void()>>;

    // A set of torrents to be removed
    // Torrent id (uint32_t) is used instead of lt::torrent_handler
    //
    std::set<std::uint32_t> m_torrentSet;

    // This handler will be called after all torrents have been removed
    RemoveNotification      m_endRemoveHandler;
};

using  RemoveTorrentContextPtr = std::shared_ptr<RemoveTorrentContext>;

//
// It provides the ability to exchange files
//
class Session {
public:

    using lt_handle = lt::torrent_handle;

    virtual ~Session() = default;

    virtual void      endSession() = 0;

    virtual lt_handle addTorrentFileToSession( const std::string& torrentFilename,
                                               const std::string& savePath,
                                               endpoint_list = {} ) = 0;

    virtual RemoveTorrentContextPtr createRemoveTorrentContext() = 0;

    // It removes torrent from session
    virtual void      removeTorrentFromSession( lt_handle, RemoveTorrentContextPtr ) = 0;

    virtual InfoHash  addActionListToSession( const ActionList&,
                                              const std::string& workFolder,
                                              endpoint_list list = {} ) = 0;

    virtual void      downloadFile( const DownloadContext& downloadParameters, endpoint_list list = {} ) = 0;

};

// createTorrentFile
InfoHash createTorrentFile( const std::string& pathToFolderOrFolder,
                            const std::string& /*pathToRootFolder*/, // noyt used
                            const std::string& outputTorrentFilename );

//
// calculateInfoHashAndTorrent (InfoHash is a part of magnetlink)
//
InfoHash calculateInfoHashAndTorrent( const std::string& pathToFile,
                                      const std::string& drivePublicKey,
                                      const std::string& outputTorrentPath );

//
// createDefaultLibTorrentSession
//

namespace libtorrent {
    struct alert;
}

using LibTorrentErrorHandler = std::function<void( libtorrent::alert* )>;

std::shared_ptr<Session> createDefaultSession( std::string address, const LibTorrentErrorHandler& );

}}

/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "types.h"
#include "ActionList.h"
#include "log.h"

#include <filesystem>
#include <libtorrent/torrent_handle.hpp>
#include <boost/asio/ip/tcp.hpp>

using  tcp = boost::asio::ip::tcp;
using  endpoint_list = std::vector<boost::asio::ip::tcp::endpoint>;

namespace sirius { namespace drive {

#define FS_TREE_FILE_NAME "FsTree.bin"


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
struct DownloadContext {

    using Notification = std::function<void( const DownloadContext&,
                                             download_status::code,
                                             const std::string& errorText )>;

//    DownloadContext( const DownloadContext& ) = default;

    DownloadContext( Notification          notification,
                     InfoHash              infoHash,
                     std::filesystem::path saveFolder,
                     std::filesystem::path renameAs = {} )
        :
          m_downloadNotification(notification),
          m_infoHash(infoHash),
          m_saveFolder(saveFolder),
          m_renameAs(renameAs)
        {}

//    ~DownloadContext() {
//        LOG("m_saveFolder:" << m_saveFolder);
//        LOG("m_renameAs:"   << m_renameAs);
//    }

    Notification          m_downloadNotification;

    InfoHash              m_infoHash;
    std::filesystem::path m_saveFolder;
    std::filesystem::path m_renameAs;

    float                 m_downloadPercents = 0.;
    // ... todo
};

//
// It will be used to inform 'client' that all required torrents
// have been sucessfully removed from the session.
// And only after that the 'client' could remove/move files and torrnet file.
//
struct RemoveTorrentContext
{
    RemoveTorrentContext(
            std::set<lt::torrent_handle>&& torrentSet,
            const std::function<void()>&   endRemoveNotification )
        :
            m_torrentSet(torrentSet),
            m_endRemoveNotification(endRemoveNotification)
        {}
    // A set of torrents to be removed
    // Torrent id (uint32_t) is used instead of lt::torrent_handler
    //
    std::set<lt::torrent_handle> m_torrentSet;

    // This handler will be called after all torrents have been removed
    std::function<void()>        m_endRemoveNotification;
};

using  RemoveContextPtr = std::shared_ptr<RemoveTorrentContext>;

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

    // It removes torrents from session.
    // After removing 'endNotification' will be called.
    // And only after that the 'client' could move/remove files and torrnet file.
    virtual void      removeTorrentsFromSession( std::set<lt::torrent_handle>&& torrents,
                                                 std::function<void()>          endNotification ) = 0;

    virtual InfoHash  addActionListToSession( const ActionList&,
                                              const std::string& workFolder,
                                              endpoint_list list = {} ) = 0;

    virtual void      downloadFile( const DownloadContext& downloadParameters, endpoint_list list = {} ) = 0;

};

// createTorrentFile
InfoHash createTorrentFile( const std::string& pathToFolderOrFolder,
                            const std::string& /*pathToRootFolder*/, // not used
                            const std::string& outputTorrentFilename );

//
// It is used on drive side only.
// It calculates modified InfoHash for 'file'
// and creates modified torrent file in 'outputTorrentPath'
// with name as 'InfoHash' + '.' + 'outputTorrentFileExtension'
//
InfoHash calculateInfoHashAndTorrent( const std::string& file,
                                      const std::string& drivePublicKey,
                                      const std::string& outputTorrentPath,
                                      const std::string& outputTorrentFileExtension );

//
// createDefaultLibTorrentSession
//

namespace libtorrent {
    struct alert;
}

using LibTorrentErrorHandler = std::function<void( const lt::alert* )>;

std::shared_ptr<Session> createDefaultSession( std::string address, const LibTorrentErrorHandler& );

}}

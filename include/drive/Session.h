/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "types.h"
#include "drive/ActionList.h"
#include "drive/log.h"

#include "crypto/Signer.h"

#include <cereal/archives/binary.hpp>

#include <filesystem>
#include <future>
#include <libtorrent/torrent_handle.hpp>
#include <boost/asio/ip/tcp.hpp>

#ifdef SIRIUS_DRIVE_MULTI
#include <sirius_drive/session_delegate.h>

//for dbg
#include <libtorrent/session.hpp>
#endif

using  endpoint_list = std::vector<boost::asio::ip::tcp::endpoint>;

namespace sirius::drive {

#define FS_TREE_FILE_NAME  "FsTree.bin"
#define PLAYLIST_FILE_NAME "playlist.m3u8"

// It will be used to inform 'client' about download status
//
namespace download_status {
    enum code {
        download_complete = 0,
        downloading = 1,
        dn_failed = 2
    };
};

// It will be used to inform 'client' about download status
//
struct DownloadContext {

    enum download_type {
        fs_tree = 0,
        file_from_drive = 1,
        client_data = 3,
        missing_files = 4,
        chunk_from_drive = 5,
    };

    using Notification = std::function<void( download_status::code,
                                             const InfoHash&,
                                             const std::filesystem::path filePath,
                                             size_t downloaded,
                                             size_t fileSize,
                                             const std::string& errorText )>;

    DownloadContext( download_type         downloadType,
                     Notification          notification,
                     const InfoHash&       infoHash,
                     const Hash256&        transactionHash,
                     uint64_t              downloadLimit, // 0 means unlimited
                     bool                  doNotDeleteTorrent = false,
                     std::filesystem::path saveAs = {} )
        :
          m_downloadType(downloadType),
          m_downloadNotification(notification),
          m_infoHash(infoHash),
          m_transactionHash(transactionHash),
          m_downloadLimit(downloadLimit),
          m_saveAs(saveAs),
          m_doNotDeleteTorrent(doNotDeleteTorrent)
        {
            if ( m_downloadType == file_from_drive && m_saveAs.empty() )
            {
                _LOG_ERR("m_downloadType == file_from_drive && m_saveAs.empty()")
            }

            if ( (m_downloadType == fs_tree || m_downloadType == client_data) && !m_saveAs.empty() )
            {
                _LOG_ERR("(m_downloadType == fs_tree || m_downloadType == client_data) && !m_saveAs.empty()")
            }
        }

    download_type         m_downloadType;

    Notification          m_downloadNotification;
    InfoHash              m_infoHash;
    Hash256               m_transactionHash;
    uint64_t              m_downloadLimit; // for modify drive - all data size
    std::filesystem::path m_saveAs;
    bool                  m_doNotDeleteTorrent = false;
    
};

class DhtMessageHandler
{
public:
    virtual ~DhtMessageHandler() = default;
    
    virtual bool on_dht_request( lt::string_view                         query,
                                 boost::asio::ip::udp::endpoint const&   source,
                                 lt::bdecode_node const&                 message,
                                 lt::entry&                              response ) = 0;

};

//
//
// It provides the ability to exchange files
//
class Session {
public:

    using lt_handle = lt::torrent_handle;

    virtual ~Session() = default;

    virtual lt::session&  lt_session() = 0;


    virtual void      endSession() = 0;
    virtual bool      isEnding() = 0;

    // It loads existing file from disk
    virtual lt_handle addTorrentFileToSession( const std::string&               torrentFilename,
                                               const std::string&               folderWhereFileIsLocated,
                                               lt::SiriusFlags::type            siriusFlags,
                                               const std::array<uint8_t,32>*    driveKey,
                                               const std::array<uint8_t,32>*    channelId,
                                               const std::array<uint8_t,32>*    modifyTx,
                                               endpoint_list = {},
                                               uint64_t* outTotalSize = nullptr ) = 0;

    // It removes torrents from session.
    // After removing 'endNotification' will be called.
    // And only after that the 'client' could move/remove files and torrnet file.
    virtual void      removeTorrentsFromSession( const std::set<lt::torrent_handle>& torrents,
                                                 std::function<void()>               endNotification ) = 0;
    
    // It starts downloading of 'modify data' (identified by downloadParameters.m_infoHash)
    // keysHints and endpointsHints are independent hits about peers to download the torrent from
    // It is not necessary to mention the hints: libtorrent will try to find the peers itself
    // But it can speed up downloading
    virtual lt_handle download( DownloadContext&&               downloadParameters,
                                const std::string&              saveFolder,
                                const std::string&              saveTorrentFolder,
                                const ReplicatorList&           keysHints,
                                const std::array<uint8_t,32>*   driveKey,
                                const std::array<uint8_t,32>*   channelId,
                                const std::array<uint8_t,32>*   modifyTx,
                                const endpoint_list&            endpointsHints = {}) = 0;

    // Remove download context
    // (It prevents call of downloadHandler)
    virtual void      removeDownloadContext( lt::torrent_handle ) = 0;

    //virtual void      sendMessage( boost::asio::ip::udp::endpoint, const std::vector<uint8_t>& ) = 0;
    virtual void      sendMessage( const std::string& query, boost::asio::ip::udp::endpoint, const std::vector<uint8_t>&, const Signature* signature = nullptr ) = 0;
    virtual void      sendMessage(const std::string& query, boost::asio::ip::udp::endpoint, const std::string& ) = 0;
    
    virtual void      findAddress( const Key& key ) = 0;
    virtual void      announceExternalAddress( const boost::asio::ip::tcp::endpoint& endpoint ) = 0;
    
    virtual void      onTorrentDeleted( lt::torrent_handle handle ) = 0;


    virtual std::optional<boost::asio::high_resolution_timer> startTimer( int miliseconds, const std::function<void()>& func ) = 0;


    // for testing and debugging
    virtual void      dbgPrintActiveTorrents() = 0;
};

// createTorrentFile
PLUGIN_API InfoHash createTorrentFile( const std::string& pathToFolderOrFolder,
                                       const Key&         drivePublicKey, // or client public key
                                       const std::string& pathToRootFolder,
                                       const std::string& outputTorrentFilename );

//
// It is used on drive side only.
// It calculates modified InfoHash for 'file'
// and creates modified torrent file in 'outputTorrentPath'
// with name as 'InfoHash' + '.' + 'outputTorrentFileExtension'
//
PLUGIN_API InfoHash calculateInfoHashAndCreateTorrentFile( const std::string& file,
                                                           const Key&         drivePublicKey, // or client public key
                                                           const std::string& outputTorrentPath,
                                                           const std::string& outputTorrentFileExtension );

PLUGIN_API InfoHash calculateInfoHash( const std::string& pathToFile, const Key& drivePublicKey );


//
// createDefaultLibTorrentSession
//

namespace libtorrent {
    struct alert;
}

class ReplicatorInt;

using LibTorrentErrorHandler = std::function<void( const lt::alert* )>;

PLUGIN_API std::shared_ptr<Session> createDefaultSession( boost::asio::io_context& context,
                                                          std::string address,
                                                          const LibTorrentErrorHandler&,
                                                          std::weak_ptr<ReplicatorInt>,
                                                          std::weak_ptr<lt::session_delegate>,
                                                          const endpoint_list& bootstraps,
                                                          std::promise<void>&& bootstrapBarrier );

PLUGIN_API std::shared_ptr<Session> createDefaultSession( std::string address,
                                                          const LibTorrentErrorHandler&,
                                                          std::weak_ptr<lt::session_delegate>,
                                                          const endpoint_list& bootstraps,
                                                          std::weak_ptr<DhtMessageHandler> dhtMessageHandler );

}

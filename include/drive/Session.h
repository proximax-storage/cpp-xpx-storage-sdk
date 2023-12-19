/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "types.h"
#include "drive/ActionList.h"
#include "drive/log.h"
#include "Timer.h"
#include "IOContextProvider.h"

#include "crypto/Signer.h"
#include "Kademlia.h"

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

using  endpoint_list = std::vector<boost::asio::ip::udp::endpoint>;

inline bool isValid( const boost::asio::ip::udp::endpoint& ep )
{
    if ( ep.address().is_v4() && ep.address().to_v4() != boost::asio::ip::address_v4::any() ) return true;
    if ( ep.address().is_v6() && ep.address().to_v6() != boost::asio::ip::address_v6::any() ) return true;
    return false;
}

namespace sirius::drive {

#define FS_TREE_FILE_NAME  "FsTree.bin"
#define PLAYLIST_FILE_NAME "playlist.m3u8"

#define GET_MY_IP_MSG       "get-my-ip"
#define GET_PEER_IP_MSG     "get-peer-ip"
#define MY_IP_RESPONSE      "my-ip-response"
#define PEER_IP_RESPONSE    "peer-ip-response"

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
        stream_data = 5,
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
          m_transactionHashX(transactionHash),
          m_downloadLimit(downloadLimit),
          m_saveAs(saveAs),
          m_doNotDeleteTorrent(doNotDeleteTorrent)
        {
//            if ( m_downloadType == file_from_drive && m_saveAs.empty() )
//            {
//                _LOG_ERR("m_downloadType == file_from_drive && m_saveAs.empty()")
//            }

            if ( (m_downloadType == fs_tree || m_downloadType == client_data) && !m_saveAs.empty() )
            {
                _LOG_ERR("(m_downloadType == fs_tree || m_downloadType == client_data) && !m_saveAs.empty()")
            }
  
// it is not true for catching-up
//            if ( doNotDeleteTorrent && !saveAs.empty() )
//            {
//                __LOG_WARN( "doNotDeleteTorrent && !saveAs.empty()" )
//            }
        }

    download_type         m_downloadType;

    Notification          m_downloadNotification;
    InfoHash              m_infoHash;
    Hash256               m_transactionHashX;
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

enum class LogMode: uint8_t {
    BRIEF,
    PEER,
    FULL
};

//
//
// It provides the ability to exchange files
//
class Session: public IOContextProvider, public kademlia::Transport {
public:

    using lt_handle = lt::torrent_handle;

    virtual ~Session() = default;

    virtual lt::session&  lt_session() = 0;


    virtual void      endSession() = 0;
    virtual bool      isEnding() = 0;

    virtual bool      isClient() = 0;
    
    virtual void      setEndpointHandler( EndpointHandler endpointHandler ) = 0;

    // Interface with Kademlia
    virtual void      startSearchPeerEndpoints( const std::vector<Key>& keys ) = 0;
    virtual void      addClientToLocalEndpointMap( const Key& keys ) = 0;
    virtual void      onEndpointDiscovered( const Key& key, const OptionalEndpoint& endpoint ) = 0;
    virtual void      addReplicatorKeyToKademlia( const Key& key ) = 0;
    virtual void      addReplicatorKeysToKademlia( const std::vector<Key>& keys ) = 0;
    virtual void      removeReplicatorKeyFromKademlia( const Key& keys ) = 0;
    virtual void      dbgTestKademlia( KademliaDbgFunc dbgFunc ) = 0;


    virtual OptionalEndpoint getEndpoint( const Key& key ) = 0;
    virtual const kademlia::PeerInfo*  getPeerInfo( const Key& key ) = 0;

    // It loads existing file from disk
    virtual lt_handle addTorrentFileToSession( const std::filesystem::path&     torrentFilename,
                                               const std::filesystem::path&     folderWhereFileIsLocated,
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
                                                 std::function<void()>               endNotification,
										   		 bool removeFiles ) = 0;
    
    // It starts downloading of 'modify data' (identified by downloadParameters.m_infoHash)
    // keysHints and endpointsHints are independent hits about peers to download the torrent from
    // It is not necessary to mention the hints: libtorrent will try to find the peers itself
    // But it can speed up downloading
    virtual lt_handle download( DownloadContext&&               downloadParameters,
                                const std::filesystem::path&    saveFolder,
                                const std::filesystem::path&    saveTorrentFilePath,
                                const ReplicatorList&           keysHints,
                                const std::array<uint8_t,32>*   driveKey,
                                const std::array<uint8_t,32>*   channelId,
                                const std::array<uint8_t,32>*   modifyTx,
                                const endpoint_list&            endpointsHints = {}) = 0;

    // Remove download context
    // (It prevents call of downloadHandler)
    virtual void      removeDownloadContext( lt::torrent_handle ) = 0;

    virtual void      sendMessage( const std::string& query, boost::asio::ip::udp::endpoint, const std::vector<uint8_t>&, const Signature* signature = nullptr ) = 0;
    virtual void      sendMessage(const std::string& query, boost::asio::ip::udp::endpoint, const std::string& ) = 0;
    
    virtual void      onTorrentDeleted( lt::torrent_handle handle ) = 0;

    virtual void      onCacheFlushed( lt::torrent_handle handle ) = 0;

    virtual void      setTorrentDeletedHandler( std::function<void(lt::torrent_handle)> ) = 0;

    virtual Timer     startTimer( int milliseconds, std::function<void()> func ) = 0;

    virtual void      connectTorentsToEndpoint( const boost::asio::ip::udp::endpoint& endpoint ) = 0;

    // for testing and debugging
    virtual void      dbgPrintActiveTorrents() = 0;

    virtual void      setLogMode( LogMode mode ) = 0;
    
    virtual int       listeningPort() = 0;
};

// createTorrentFile
PLUGIN_API InfoHash createTorrentFile( const std::filesystem::path& pathToFolderOrFolder,
                                       const Key&                   drivePublicKey, // or client public key
                                       const std::filesystem::path& pathToRootFolder,
                                       const std::filesystem::path& outputTorrentFilename );

//
// It is used on drive side only.
// It calculates modified InfoHash for 'file'
// and creates modified torrent file in 'outputTorrentPath'
// with name as 'InfoHash' + '.' + 'outputTorrentFileExtension'
//
PLUGIN_API InfoHash calculateInfoHashAndCreateTorrentFile( const std::filesystem::path& file,
                                                           const Key&                   drivePublicKey, // or client public key
                                                           const std::filesystem::path& outputTorrentPath,
                                                           const std::filesystem::path& outputTorrentFileExtension );

PLUGIN_API InfoHash calculateInfoHash( const std::filesystem::path& pathToFile, const Key& drivePublicKey );


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
                                                          const std::vector<ReplicatorInfo>& bootstraps,
                                                          std::promise<void>&& bootstrapBarrier );

PLUGIN_API std::shared_ptr<Session> createDefaultSession( std::string address,
                                                          const crypto::KeyPair&,
                                                          const LibTorrentErrorHandler&,
                                                          std::weak_ptr<lt::session_delegate>,
                                                          const std::vector<ReplicatorInfo>& bootstraps,
                                                          std::weak_ptr<DhtMessageHandler> dhtMessageHandler );

}

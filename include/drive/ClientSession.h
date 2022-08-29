/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "drive/Session.h"
#include "drive/log.h"
#include "drive/Utils.h"
#include "crypto/Signer.h"
#include <sirius_drive/session_delegate.h>

namespace sirius::drive {

class StreamerSession;
class ViewerSession;

class ClientSession : public lt::session_delegate, public std::enable_shared_from_this<ClientSession>
{
protected:
    
    using DownloadChannelId     = std::optional<std::array<uint8_t,32>>;
    using ModifyTransactionHash = std::optional<std::array<uint8_t,32>>;
    using ReplicatorTraficMap   = std::map<std::array<uint8_t,32>,uint64_t>;

    struct ModifyTorrentInfo {
        Session::lt_handle  m_ltHandle = {};
        bool                m_isUsed = true;
    };
    using TorrentMap = std::map<InfoHash,ModifyTorrentInfo>;

    struct DownloadChannel {
        std::vector<Key>            m_downloadReplicatorList;
        std::map<Key, uint64_t>     m_requestedSize;
        std::map<Key, uint64_t>     m_receivedSize;
    };

    std::shared_ptr<Session>    m_session;
    const crypto::KeyPair&      m_keyPair;
    
    TorrentMap                          m_modifyTorrentMap;
    std::map<Hash256, DownloadChannel>  m_downloadChannelMap;

    const char*                 m_dbgOurPeerName;

public:
    ClientSession( const crypto::KeyPair& keyPair, const char* dbgOurPeerName )
    :
        m_keyPair(keyPair),
        m_dbgOurPeerName(dbgOurPeerName)
    {}

    ~ClientSession()
    {
        _LOG( "ClientSession deleted" );
    }
public:

    const std::array<uint8_t,32>& publicKey() override
    {
        return m_keyPair.publicKey().array();
    }

    virtual void onTorrentDeleted( lt::torrent_handle handle ) override
    {
        m_session->onTorrentDeleted( handle );
    }

    //
    // TODO Wrong
    void addDownloadChannel( Hash256 downloadChannelId )
    {
        if ( !m_downloadChannelMap.contains(downloadChannelId) )
        {
            m_downloadChannelMap[downloadChannelId] = {};
        }
    }

    void setDownloadChannelReplicators( const Hash256& downloadChannelId, const ReplicatorList& replicators )
    {
        auto it = m_downloadChannelMap.find(downloadChannelId);
        if (it == m_downloadChannelMap.end()) {
            return;
        }
        it->second.m_downloadReplicatorList = replicators;
    }

    void setDownloadChannelRequestedSizes( const Hash256& downloadChannelId, const std::map<Key, uint64_t>& sizes )
    {
        auto it = m_downloadChannelMap.find(downloadChannelId);
        if (it == m_downloadChannelMap.end()) {
            return;
        }
        it->second.m_requestedSize = sizes;
    }

    void setDownloadChannelReceivedSizes( const Hash256& downloadChannelId, const std::map<Key, uint64_t>& sizes )
    {
        auto it = m_downloadChannelMap.find(downloadChannelId);
        if (it == m_downloadChannelMap.end()) {
            return;
        }
        it->second.m_requestedSize = sizes;
    }

    void onHandshake( uint64_t /*uploadedSize*/ ) override
    {
        //todo here could be call back-call of test UI app
    }

    // Initiate file downloading (identified by downloadParameters.m_infoHash)
    void download( DownloadContext&&      downloadParameters,
                   const Hash256&         downloadChannelId,
                   const std::string&     saveFolder,
                   const std::string&     saveTorrentFolder,
                   const endpoint_list&   endpointsHints = {})
    {
        // check that download channel was set
        if ( !m_downloadChannelMap.contains(downloadChannelId) )
            throw std::runtime_error("downloadChannel does not exist");

        const auto& downloadChannel = m_downloadChannelMap[downloadChannelId];

        downloadParameters.m_transactionHash = downloadChannelId;
        
        if ( auto it = m_modifyTorrentMap.find( downloadParameters.m_infoHash ); it != m_modifyTorrentMap.end() )
        {
            auto tHandle = it->second.m_ltHandle;
            auto status = tHandle.status( lt::torrent_handle::query_save_path | lt::torrent_handle::query_name );
            auto filePath = fs::path( status.save_path ) / status.name;
            if ( fs::exists(filePath) && ! fs::is_directory(filePath) )
            {
                try {
                    __LOG( "download: copy '" << filePath << "'" << " to '" << downloadParameters.m_saveAs << "'" )
                    fs::copy( filePath, downloadParameters.m_saveAs );
                    downloadParameters.m_downloadNotification( download_status::download_complete,
                                                              downloadParameters.m_infoHash,
                                                              downloadParameters.m_saveAs,
                                                              0,
                                                              0,
                                                              "" );
                    return;
                } catch(...) {}
            }
            else
            {
                _LOG_WARN( "Invalid modify torrent? ");
            }
        }

        auto downloadChannelIdAsArray = downloadChannelId.array();

        // start downloading
        m_session->download( std::move(downloadParameters),
                             saveFolder,
                             saveTorrentFolder,
                             downloadChannel.m_downloadReplicatorList,
                             nullptr,
                             &downloadChannelIdAsArray,
                             nullptr,
                             endpointsHints );
    }

    std::optional<boost::asio::ip::tcp::endpoint> getEndpoint(const std::array<uint8_t, 32> &key) override
    {
            return {};
    }

    InfoHash addActionListToSession( const ActionList&      actionList,
                                     const Key&             drivePublicKey,
                                     const ReplicatorList&  unused,
                                     const std::string&     sandboxFolder, // it is the folder where all ActionLists and file-links will be placed
                                     uint64_t&              outTotalModifySize,
                                     const endpoint_list&   endpointList = {}
                                   )
    {
        fs::path workFolder = sandboxFolder;
        std::error_code ec;
        fs::create_directories( workFolder, ec );

        outTotalModifySize = 0;

        // Create new action list
        //
        ActionList newActionList = actionList;
        for( auto& action : newActionList )
        {
            switch ( action.m_actionId )
            {
                case action_list_id::upload:
                {
                    if ( ! fs::exists(action.m_param1) )
                    {
                        throw std::runtime_error( std::string("File is absent: ") + action.m_param1 );
                        break;
                    }

                    if ( fs::is_directory(action.m_param1) )
                    {
                        throw std::runtime_error( std::string("Folder could not be added, only files: ") + action.m_param1 );
                        break;
                    }

                    // calculate InfoHash
                    InfoHash infoHash = createTorrentFile( action.m_param1, drivePublicKey, fs::path(action.m_param1).parent_path().string(), {} );
                    __LOG( "addActionListToSession: " << infoHash << " " << action.m_param1 )
                    if ( m_modifyTorrentMap.find(infoHash) == m_modifyTorrentMap.end() )
                    {
                        fs::path filenameInSandbox = workFolder/hashToFileName(infoHash);
                        if ( ! fs::exists( filenameInSandbox ) )
                        {
                            try
                            {
                                fs::create_symlink( action.m_param1, filenameInSandbox );
                            }
                            catch(...)
                            {
                                throw std::runtime_error( "Internal error: fs::create_symlink( action.m_param1, filenameInSandbox );" );
                            }
                        }
                    }
                    action.m_filename = fs::path( action.m_param1 ).filename().string();
                    action.m_param1 = hashToFileName(infoHash);
                    break;
                }
                case action_list_id::move:
                {
                    if ( isPathInsideFolder( action.m_param1, action.m_param2 ) )
                    {
                        LOG( action.m_param1 );
                        LOG( action.m_param2 );
                        throw std::runtime_error( "invalid 'move/rename' action (destination is a child folder): " + action.m_param1
                        + " -> " + action.m_param2 );
                    }
                    break;
                }
                default:
                    break;
            }
        }

        newActionList.serialize( (workFolder/"actionList.bin").string() );

        InfoHash infoHash0 = createTorrentFile( (workFolder/"actionList.bin").string(), drivePublicKey, workFolder.string(), {} );

        if ( m_modifyTorrentMap.find(infoHash0) == m_modifyTorrentMap.end() )
        {
            fs::path filenameInSandbox = workFolder/hashToFileName(infoHash0);
            fs::path torrentFilenameInSandbox = filenameInSandbox;
            torrentFilenameInSandbox.replace_extension(".torrent");
            try
            {
                fs::rename( workFolder/"actionList.bin", filenameInSandbox );
            }
            catch(...)
            {
                throw std::runtime_error( "Internal error: fs::rename( workFolder/actionList.bin, filenameInSandbox );" );
            }

            InfoHash infoHash2 = createTorrentFile( filenameInSandbox.string(), drivePublicKey, workFolder.string(), torrentFilenameInSandbox.string() );
            uint64_t totalSize = 0;
            lt_handle torrentHandle = m_session->addTorrentFileToSession( torrentFilenameInSandbox.string(),
                                                                          workFolder.string(),
                                                                          lt::SiriusFlags::client_has_modify_data,
                                                                          &infoHash0.array(),
                                                                          nullptr,
                                                                          &drivePublicKey.array(),
                                                                          endpointList,
                                                                          &totalSize );
            outTotalModifySize += totalSize;
            m_modifyTorrentMap[infoHash2] = {torrentHandle,false};
        }
        
        for( auto& action : newActionList )
        {
            switch ( action.m_actionId )
            {
                case action_list_id::upload:
                {
                    InfoHash infoHash = stringToByteArray<Hash256>( action.m_param1 );
                    if ( m_modifyTorrentMap.find(infoHash) == m_modifyTorrentMap.end() )
                    {
                        fs::path filenameInSandbox = workFolder/action.m_param1;
                        fs::path torrentFilenameInSandbox = filenameInSandbox;
                        torrentFilenameInSandbox.replace_extension(".torrent");

                        InfoHash infoHash2 = createTorrentFile( filenameInSandbox.string(), drivePublicKey, workFolder.string(), torrentFilenameInSandbox.string() );
                        __ASSERT( infoHash == infoHash2 );
                        
                        uint64_t totalSize = 0;
                        lt_handle torrentHandle = m_session->addTorrentFileToSession( torrentFilenameInSandbox.string(),
                                                                                      workFolder.string(),
                                                                                      lt::SiriusFlags::client_has_modify_data,
                                                                                      &infoHash0.array(),
                                                                                      nullptr,
                                                                                      &drivePublicKey.array(),
                                                                                      endpointList,
                                                                                      &totalSize );
                        outTotalModifySize += totalSize;
                        m_modifyTorrentMap[infoHash2] = {torrentHandle,false};
                    }
                    break;
                }
                case action_list_id::move:
                {
                    if ( isPathInsideFolder( action.m_param1, action.m_param2 ) )
                    {
                        LOG( action.m_param1 );
                        LOG( action.m_param2 );
                        throw std::runtime_error( "invalid 'move/rename' action (destination is a child folder): " + action.m_param1
                        + " -> " + action.m_param2 );
                    }
                    break;
                }
                default:
                    break;
            }
        }

        return infoHash0;
    }

    void removeTorrents()
    {
        std::set<lt::torrent_handle>  torrents;
        
        for( auto& [key,value]: m_modifyTorrentMap )
        {
            __LOG( "removeModifyTorrents: " << key )
            torrents.insert( value.m_ltHandle );
        }
        
        //m_session->dbgPrintActiveTorrents();
        
        std::promise<void> barrier;

        boost::asio::post(m_session->lt_session().get_context(), [&torrents,&barrier,this]() //mutable
        {
            m_session->removeTorrentsFromSession( torrents, [&barrier] {
                __LOG("???? barrier.set_value();")
                barrier.set_value();
            }, true);
        });
        //m_session->dbgPrintActiveTorrents();
        barrier.get_future().wait();
        
        m_modifyTorrentMap.clear();
    }

    void removeTorrents(const std::vector<std::array<uint8_t,32>>& hashes)
    {
        std::set<lt::torrent_handle>  torrents;
        for (const auto& hash : hashes) {
            if (m_modifyTorrentMap.contains(hash)) {
                torrents.insert( m_modifyTorrentMap[hash].m_ltHandle );
                m_modifyTorrentMap.erase(hash);
            }
        }

        std::promise<void> barrier;
        boost::asio::post(m_session->lt_session().get_context(), [&torrents,&barrier,this]() //mutable
        {
            m_session->removeTorrentsFromSession( torrents, [&barrier] {
                __LOG("???? barrier.set_value();")
                barrier.set_value();
            }, true);
        });

        barrier.get_future().wait();
    }

    void setSessionSettings(const lt::settings_pack& settings, bool localNodes)
    {
        m_session->lt_session().apply_settings(settings);
        if (localNodes) {
            std::uint32_t const mask = 1 << lt::session::global_peer_class_id;
            lt::ip_filter f;
            f.add_rule(lt::make_address("0.0.0.0"), lt::make_address("255.255.255.255"), mask);
            m_session->lt_session().set_peer_class_filter(f);
        }
    }

    void stop()
    {
        m_session->endSession();
        auto blockedDestructor = m_session->lt_session().abort();
        m_session.reset();
    }

    // The next functions are called in libtorrent
protected:

    bool isClient() const override { return true; }
    
    lt::connection_status acceptClientConnection( const std::array<uint8_t,32>&  /*channelId*/,
                                                  const std::array<uint8_t,32>&  /*peerKey*/,
                                                  const std::array<uint8_t,32>&  /*driveKey*/,
                                                  const std::array<uint8_t,32>&  /*fileHash*/ ) override
    {
        return lt::connection_status::UNLIMITED;
    }

    lt::connection_status acceptReplicatorConnection( const std::array<uint8_t,32>&  /*transactionHash*/,
                                     const std::array<uint8_t,32>&  /*peerPublicKey*/ ) override
    {
        return lt::connection_status::UNLIMITED;
    }

    void onDisconnected( const std::array<uint8_t,32>&  transactionHash,
                         const std::array<uint8_t,32>&  peerPublicKey,
                         int                            reason ) override
    {
//        _LOG( "onDisconnected: " << dbgOurPeerName() << " from replicator: " << (int)peerPublicKey[0] );
//        _LOG( " - requestedSize: " << m_requestedSize[peerPublicKey] );
//        _LOG( " - receivedSize:  " << m_receivedSize[peerPublicKey] );
    }

    bool checkDownloadLimit( const std::array<uint8_t,32>& /*reciept*/,
                             const std::array<uint8_t,32>& /*downloadChannelId*/,
                             uint64_t                      /*downloadedSize*/ ) override
    {
        // client does not check download limit
        return true;
    }

    virtual void signReceipt( const std::array<uint8_t,32>& downloadChannelId,
                              const std::array<uint8_t,32>& replicatorPublicKey,
                              uint64_t                      downloadedSize,
                              std::array<uint8_t,64>&       outSignature ) override
    {
        {
//todo++
//            _LOG( "SSS downloadChannelId: " << Key(downloadChannelId) );
//            _LOG( "SSS " << dbgOurPeerName() << " " << int(downloadChannelId[0]) << " " << (int)publicKey()[0] << " " << (int) replicatorPublicKey[0] << " " << downloadedSize );
            crypto::Sign( m_keyPair,
                          {
                            utils::RawBuffer{downloadChannelId},
                            utils::RawBuffer{publicKey()},
                            utils::RawBuffer{replicatorPublicKey},
                            utils::RawBuffer{(const uint8_t*)&downloadedSize,8}
                          },
                          reinterpret_cast<Signature&>(outSignature) );

//todo++
//            _LOG( "SSS: " << int(outSignature[0]) );
        }

        //todo++
//        if ( !verifyReceipt( downloadChannelId,
//                            publicKey(),       // client public key
//                            replicatorPublicKey,   // replicator public key
//                            downloadedSize, outSignature ) )
//        {
//            assert(0);
//        }
    }

    void signHandshake( const uint8_t* bytes, size_t size, std::array<uint8_t,64>& signature ) override
    {
        crypto::Sign( m_keyPair, utils::RawBuffer{bytes,size}, reinterpret_cast<Signature&>(signature) );
        //_LOG( "SIGN HANDSHAKE: " << int(signature[0]) )
    }

    virtual bool verifyHandshake( const uint8_t* bytes, size_t size,
                                  const std::array<uint8_t,32>& publicKey,
                                  const std::array<uint8_t,64>& signature ) override
    {
        //_LOG( "verifyHandshake: " << int(signature[0]) )

        bool ok = crypto::Verify( publicKey, utils::RawBuffer{bytes,size}, signature );
        if ( !ok )
            _LOG( "verifyHandshake: failed" )
        return ok;
    }

    bool verifyMutableItem(const std::vector<char>& value,
                               const int64_t& seq,
                               const std::string& salt,
                               const std::array<uint8_t, 32>& pk,
                               const std::array<uint8_t, 64>& sig) override
    {
        return crypto::Verify(Key{pk},
                              {
                                      utils::RawBuffer{reinterpret_cast<const uint8_t *>(value.data()),
                                                       value.size()},
                                      utils::RawBuffer{reinterpret_cast<const uint8_t *>(&seq), sizeof(int64_t)},
                                      utils::RawBuffer{reinterpret_cast<const uint8_t *>(salt.data()), salt.size()}
                              },
                              reinterpret_cast<const Signature &>(sig));
    }

    void signMutableItem(const std::vector<char> &value,
                         const int64_t& seq,
                         const std::string& salt,
                         std::array<uint8_t, 64>& sig) override
    {
        crypto::Sign(m_keyPair,
                     {
                             utils::RawBuffer{reinterpret_cast<const uint8_t *>(value.data()), value.size()},
                             utils::RawBuffer{reinterpret_cast<const uint8_t *>(&seq), sizeof(int64_t)},
                             utils::RawBuffer{reinterpret_cast<const uint8_t *>(salt.data()), salt.size()}
                     },
                     reinterpret_cast<Signature &>(sig));
    }

//    void setStartReceivedSize( uint64_t downloadedSize ) override
//    {
//        // 'downloadedSize' should be set to proper value (last 'downloadedSize' of peviuos peer_connection)
//        m_receivedSize = downloadedSize;
//    }

    void onPieceRequestWrite( const std::array<uint8_t,32>&  transactionHash,
                              const std::array<uint8_t,32>&  senderPublicKey,
                              uint64_t                       pieceSize ) override
    {
        auto it = m_downloadChannelMap.find(transactionHash);
        if (it == m_downloadChannelMap.end())
        {
            return;
        }
        it->second.m_requestedSize[senderPublicKey] += pieceSize;
        //__LOG( "#*** onPieceRequest: " << int(senderPublicKey[0])<< ": " << m_requestedSize[senderPublicKey] )
    }
    
    bool onPieceRequestReceivedFromReplicator( const std::array<uint8_t,32>&  transactionHash,
                                               const std::array<uint8_t,32>&  receiverPublicKey,
                                               uint64_t                       pieceSize ) override
    {
        return true;
    }

    bool onPieceRequestReceivedFromClient( const std::array<uint8_t,32>&  transactionHash,
                                           const std::array<uint8_t,32>&  receiverPublicKey,
                                           uint64_t                       pieceSize ) override
    {
        return true;
    }

    void onPieceSent( const std::array<uint8_t,32>&  transactionHash,
                      const std::array<uint8_t,32>&  receiverPublicKey,
                      uint64_t                       pieceSize ) override
    {
        //todo++
    }

    void onPieceReceived( const std::array<uint8_t,32>&  transactionHash,
                          const std::array<uint8_t,32>&  senderPublicKey,
                          uint64_t                       pieceSize ) override
    {
        auto it = m_downloadChannelMap.find(transactionHash);
        if (it == m_downloadChannelMap.end()) {
            return;
        }
        it->second.m_receivedSize[senderPublicKey] += pieceSize;
    }

    uint64_t requestedSize( const std::array<uint8_t,32>&  transactionHash,
                            const std::array<uint8_t,32>&  peerPublicKey ) override
    {
        auto it = m_downloadChannelMap.find(transactionHash);
        if (it == m_downloadChannelMap.end()) {
            return 0;
        }
        return it->second.m_requestedSize[peerPublicKey];
    }

    uint64_t receivedSize( const std::array<uint8_t,32>&  transactionHash,
                           const std::array<uint8_t,32>&  peerPublicKey ) override
    {
        auto it = m_downloadChannelMap.find(transactionHash);
        if (it == m_downloadChannelMap.end()) {
            return 0;
        }
        return it->second.m_receivedSize[peerPublicKey];
    }
    
//    virtual void onMessageReceived( const std::string& query, const std::string& message, const boost::asio::ip::udp::endpoint& source ) override
//    {
//    }
    
    void handleDhtResponse( lt::bdecode_node response, boost::asio::ip::udp::endpoint endpoint ) override
    {
    }

    const char* dbgOurPeerName() override
    {
        return m_dbgOurPeerName;
    }

private:
    friend std::shared_ptr<ClientSession>     createClientSession( const crypto::KeyPair&,
                                                                   const std::string&,
                                                                   const LibTorrentErrorHandler&,
                                                                   const endpoint_list&,
                                                                   bool,
                                                                   const char* );

    friend std::shared_ptr<StreamerSession> createStreamerSession( const crypto::KeyPair&,
                                                                   const std::string&,
                                                                   const LibTorrentErrorHandler&,
                                                                   const endpoint_list&,
                                                                   bool,
                                                                   const char* );

    friend std::shared_ptr<ViewerSession> createViewerSession( const crypto::KeyPair&,
                                                               const std::string&,
                                                               const LibTorrentErrorHandler&,
                                                               const endpoint_list&,
                                                               bool,
                                                               const char* );

    auto session() { return m_session; }
};

// ClientSession creator
inline std::shared_ptr<ClientSession> createClientSession(  const crypto::KeyPair&        keyPair,
                                                            const std::string&            address,
                                                            const LibTorrentErrorHandler& errorHandler,
                                                            const endpoint_list&          bootstraps,
                                                            bool                          useTcpSocket, // instead of uTP
                                                            const char*                   dbgClientName = "" )
{
    //LOG( "creating: " << dbgClientName << " with key: " <<  int(keyPair.publicKey().array()[0]) )

    std::shared_ptr<ClientSession> clientSession = std::make_shared<ClientSession>( keyPair, dbgClientName );
    clientSession->m_session = createDefaultSession( address, errorHandler, clientSession, bootstraps, {} );
    clientSession->session()->lt_session().m_dbgOurPeerName = dbgClientName;
    clientSession->addDownloadChannel(Hash256());
    return clientSession;
}

}

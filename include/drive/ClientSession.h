/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "drive/Session.h"
#include "drive/Replicator.h"
#include "drive/RcptMessage.h"
#include "drive/log.h"
#include "drive/Utils.h"
#include "crypto/Signer.h"
#include "EndpointsManager.h"
#include <sirius_drive/session_delegate.h>
#include <boost/asio/ip/tcp.hpp>

namespace sirius::drive {

class StreamerSession;
class ViewerSession;

class ClientSession : public lt::session_delegate, public std::enable_shared_from_this<ClientSession>
{
protected:
    
    using DownloadChannelId     = std::optional<std::array<uint8_t,32>>;
    using ModifyTransactionHash = std::optional<std::array<uint8_t,32>>;
    using ReplicatorTraficMap   = std::map<std::array<uint8_t,32>,uint64_t>;

    using ErrorHandler          =  std::function<void( lt::close_reason_t            errorCode,
                                                       const std::array<uint8_t,32>& replicatorKey,
                                                       const std::array<uint8_t,32>& channelHash,
                                                       const std::array<uint8_t,32>& infoHash )>;

    using ChannelStatusResponseHandler      = std::function<void( const ReplicatorKey&          replicatorKey,
                                                                  const ChannelId&              channelId,
                                                                  const DownloadChannelInfo&    msg,
                                                                  const std::string&            error )>;

    using ModificationStatusResponseHandler = std::function<void( const ReplicatorKey&      replicatorKey,
                                                                  const Hash256&            modificationHash,
                                                                  const ModifyTrafficInfo&  msg,
                                                                  lt::string_view           currentTask,
                                                                  bool                      isModificationQueued,
                                                                  bool                      isModificationFinished,
                                                                  const std::string&        error )>;

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

    EndpointsManager            m_endpointsManager;

    TorrentMap                          m_modifyTorrentMap;
    std::map<Hash256, DownloadChannel>  m_downloadChannelMap;
    
    std::optional<ErrorHandler>         m_errorHandler;
    
    std::optional<ChannelStatusResponseHandler>         m_channelStatusResponseHandler      = defaultChannelStatusResponseHandler;
    std::optional<ModificationStatusResponseHandler>    m_modificationStatusResponseHandler = defaultModificationStatusResponseHandler;

    const char*                 m_dbgOurPeerName;

public:
    ClientSession( const crypto::KeyPair& keyPair, const char* dbgOurPeerName )
    :
        m_keyPair(keyPair),
        m_endpointsManager(keyPair, {}, dbgOurPeerName),
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

    void setErrorHandler( ErrorHandler errorHandler )
    {
        m_errorHandler = errorHandler;
    }
    
    void setEndpointHandler()
    {
        m_endpointsManager.setEndpointHandler( [self=weak_from_this()] (const Key& key, const std::optional<boost::asio::ip::tcp::endpoint>& endpoint) {
            assert( endpoint );
            if ( auto ptr = self.lock(); ptr )
            {
                __LOG( "connect to: " << *endpoint << " " << key )
                ptr->m_session->connectTorentsToEndpoint( *endpoint );
            }
        });
    }
    
    void setChannelStatusResponseHandler( ChannelStatusResponseHandler handler )
    {
        m_channelStatusResponseHandler = handler;
    }
    
    bool sendChannelStatusRequestToReplicator( const Key&     replicatorKey,
                                               const Key&     driveKey,
                                               const Hash256& downloadChannelId,
                                               std::optional<boost::asio::ip::tcp::endpoint> replicatorEndpoint = {} )
    {
        if ( ! replicatorEndpoint )
        {
            replicatorEndpoint = m_endpointsManager.getEndpoint( replicatorKey );
        }
        if ( ! replicatorEndpoint )
        {
            return false;
        }

        std::vector<uint8_t> message;
        message.insert( message.end(), m_keyPair.publicKey().begin(), m_keyPair.publicKey().end() );
        message.insert( message.end(), driveKey.begin(), driveKey.end() );
        message.insert( message.end(), downloadChannelId.begin(), downloadChannelId.end() );

        Signature signature;
        crypto::Sign( m_keyPair, { utils::RawBuffer{ message } }, signature);

        m_session->sendMessage( "get_channel_status", { replicatorEndpoint->address(), replicatorEndpoint->port() }, message, &signature );
        
        return true;
    }

    void setModificationStatusResponseHandler( ModificationStatusResponseHandler handler )
    {
        m_modificationStatusResponseHandler = handler;
    }
    
    bool sendModificationStatusRequestToReplicator( const Key&     replicatorKey,
                                                    const Key&     driveKey,
                                                    const Hash256& modificationHash,
                                                    std::optional<boost::asio::ip::tcp::endpoint> replicatorEndpoint = {} )
    {
        if ( ! replicatorEndpoint )
        {
            replicatorEndpoint = m_endpointsManager.getEndpoint( replicatorKey );
        }
        if ( ! replicatorEndpoint )
        {
            return false;
        }

        std::vector<uint8_t> message;
        message.insert( message.end(), m_keyPair.publicKey().begin(), m_keyPair.publicKey().end() );
        message.insert( message.end(), driveKey.begin(), driveKey.end() );
        message.insert( message.end(), modificationHash.begin(), modificationHash.end() );

        Signature signature;
        crypto::Sign( m_keyPair, { utils::RawBuffer{ message } }, signature);

        m_session->sendMessage( "get_modification_status", { replicatorEndpoint->address(), replicatorEndpoint->port() }, message, &signature );
        
        return true;
    }

    InfoHash addActionListToSession( const ActionList&      actionList,
                                     const Key&             drivePublicKey,
                                     const ReplicatorList&  replicatorKeys,
                                     const std::string&     sandboxFolder, // it is the folder where all ActionLists and file-links will be placed
                                     uint64_t&              outTotalModifySize,
                                     endpoint_list          endpointList = {}
                                   )
    {
        for( const auto& key: replicatorKeys )
        {
            auto endpoint = m_endpointsManager.getEndpoint( key );
            if ( endpoint )
            {
                auto it = std::find_if( endpointList.begin(), endpointList.end(), [&endpoint] (const auto& item) {
                    return item == endpoint;
                });
                if ( it == endpointList.end() )
                {
                    endpointList.push_back( *endpoint );
                }
            }
        }
        
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
                                // remove symlink and not the file
                                fs::remove( filenameInSandbox );
                                fs::create_symlink( action.m_param1, filenameInSandbox );
                            }
                            catch( const std::filesystem::filesystem_error& err ) {
                                __LOG( "ERRROR: " << err.what() << err.path1() << " " << err.path2() );
                                throw std::runtime_error( "Internal error: fs::create_symlink( action.m_param1, filenameInSandbox );" );
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

    void addDownloadChannel( const Hash256& downloadChannelId )
    {
        if ( !m_downloadChannelMap.contains(downloadChannelId) )
        {
            m_downloadChannelMap[downloadChannelId] = {};
        }
    }

    void addReplicatorList( const sirius::drive::ReplicatorList& keys )
    {
        m_endpointsManager.addEndpointsEntries( keys );
    }

    void setDownloadChannelReplicators( const Hash256& downloadChannelId, const ReplicatorList& replicators )
    {
        auto it = m_downloadChannelMap.find(downloadChannelId);
        if (it == m_downloadChannelMap.end()) {
            return;
        }
        it->second.m_downloadReplicatorList = replicators;
    }

    // Initiate file downloading (identified by downloadParameters.m_infoHash)
    lt_handle download( DownloadContext&&     downloadParameters,
                       const Hash256&         downloadChannelId,
                       const std::string&     saveFolder,
                       const std::string&     saveTorrentFolder,
                       const endpoint_list&   endpointsHints = {},
                       const ReplicatorList&  replicatorList = {} )
    {
        // check that download channel was set
        if ( !m_downloadChannelMap.contains(downloadChannelId) )
            throw std::runtime_error("downloadChannel does not exist");

        const auto& downloadChannel = m_downloadChannelMap[downloadChannelId];

        downloadParameters.m_transactionHashX = downloadChannelId;
        
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
                    return tHandle;
                } catch(...) {}
            }
            else
            {
                _LOG_WARN( "Invalid modify torrent? ");
            }
        }

        auto downloadChannelIdAsArray = downloadChannelId.array();

        ReplicatorList replicators = downloadChannel.m_downloadReplicatorList;
        for( const auto& key: replicatorList )
        {
            auto it = std::find_if( replicators.begin(), replicators.end(), [&key] (const auto& item) {
                return item == key;
            });
            if ( it == replicators.end() )
            {
                replicators.push_back( key );
            }
        }
        replicators.insert( replicators.end(), replicatorList.begin(), replicatorList.end() );

        // start downloading
        return m_session->download( std::move(downloadParameters),
                                    saveFolder,
                                    saveTorrentFolder,
                                    replicators,
                                    nullptr,
                                    &downloadChannelIdAsArray,
                                    nullptr,
                                    endpointsHints );
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

protected:
    void onError( lt::close_reason_t            errorCode,
                  const std::array<uint8_t,32>& replicatorKey,
                  const std::array<uint8_t,32>& channelHash,
                  const std::array<uint8_t,32>& infoHash ) override
    {
        if ( m_errorHandler )
        {
            (*m_errorHandler)( errorCode, replicatorKey, channelHash, infoHash );
        }
    }

    void onLastMyReceipt( const std::vector<uint8_t> receipt, const std::unique_ptr<std::array<uint8_t,32>>& channelId ) override
    {
        const RcptMessage& msg = reinterpret_cast<const RcptMessage&>(receipt);
        
        if ( ! msg.isValidSize() )
        {
            _LOG_WARN( "Bad last receipt size: " << receipt.size() );
            return;
        }
        
        _ASSERT(channelId);
        if ( msg.channelId() != *channelId )
        {
            _LOG_WARN( "Bad channelId: " << toString(msg.channelId()) << " != " << toString(*channelId) );
            return;
        }
        
//        if ( msg.clientKey() != m_keyPair.publicKey().array() )
//        {
//            _LOG_WARN( "Bad clientKey: " << toString(msg.clientKey().array()) );
//            return;
//        }
        
        // Check sign
        if ( ! crypto::Verify( m_keyPair.publicKey(),// msg.clientKey(),
                               {
                                    utils::RawBuffer{msg.channelId()},
                                    utils::RawBuffer{msg.clientKey()},
                                    utils::RawBuffer{msg.replicatorKey()},
                                    utils::RawBuffer{(const uint8_t*)msg.downloadedSizePtr(),8}
                               },
                               reinterpret_cast<const Signature&>(msg.signature()) ))
        {
            _LOG( "msg.channelId() " << msg.channelId() )
            _LOG( "msg.clientKey() " << msg.clientKey() )
            _LOG( "msg.replicatorKey() " << msg.replicatorKey() )
            _LOG( "downloadedSize: " << *msg.downloadedSizePtr() )
            _LOG( "msg.signature() " << int(msg.signature()[0]) )

            _LOG_WARN( dbgOurPeerName() << ": verifyReceipt: invalid signature will be ignored: " << int(msg.channelId()[0]) << " " << int(msg.replicatorKey()[0]) )
            return;
        }

        if ( m_downloadChannelMap[ msg.channelId().array() ].m_requestedSize[ msg.replicatorKey().array() ] < *msg.downloadedSizePtr() )
        {
            m_downloadChannelMap[ msg.channelId().array() ].m_requestedSize[ msg.replicatorKey().array() ] = *msg.downloadedSizePtr();
        }
        m_downloadChannelMap[ msg.channelId().array() ].m_receivedSize[ msg.replicatorKey().array() ] = 0;//*msg.downloadedSizePtr();
    }

    virtual void onTorrentDeleted( lt::torrent_handle handle ) override
    {
        m_session->onTorrentDeleted( handle );
    }

//    void setDownloadChannelRequestedSizes( const Hash256& downloadChannelId, const std::map<Key, uint64_t>& sizes )
//    {
//        auto it = m_downloadChannelMap.find(downloadChannelId);
//        if (it == m_downloadChannelMap.end()) {
//            return;
//        }
//        it->second.m_requestedSize = sizes;
//    }
//
//    void setDownloadChannelReceivedSizes( const Hash256& downloadChannelId, const std::map<Key, uint64_t>& sizes )
//    {
//        auto it = m_downloadChannelMap.find(downloadChannelId);
//        if (it == m_downloadChannelMap.end()) {
//            return;
//        }
//        it->second.m_requestedSize = sizes;
//    }

    void onHandshake( uint64_t /*uploadedSize*/ ) override
    {
        //todo here could be call back-call of test UI app
    }

    std::optional<boost::asio::ip::tcp::endpoint> getEndpoint(const std::array<uint8_t, 32> &key) override
    {
        return m_endpointsManager.getEndpoint(key);
    }

    std::vector<std::array<uint8_t,32>> getTorrentHandleHashes() {
        std::vector<std::array<uint8_t,32>> hashes;
        hashes.reserve(m_modifyTorrentMap.size());
        for( auto& [key,value]: m_modifyTorrentMap )
        {
            hashes.push_back(key.array());
        }

        return hashes;
    }

public:
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

    void setTorrentDeletedHandler( std::function<void(lt::torrent_handle)> handler )
    {
        m_session->setTorrentDeletedHandler( handler );
    }


    // The next functions are called in libtorrent
protected:

    bool isClient() const override { return true; }
    
    lt::connection_status acceptClientConnection( const std::array<uint8_t,32>&  /*channelId*/,
                                                  const std::array<uint8_t,32>&  /*peerKey*/,
                                                  const std::array<uint8_t,32>&  /*driveKey*/,
                                                  const std::array<uint8_t,32>&  /*fileHash*/,
                                                  lt::errors::error_code_enum&   outErrorCode ) override
    {
        outErrorCode = lt::errors::no_error;
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

    bool checkDownloadLimit( const std::array<uint8_t,32>& /*clientKey*/,
                             const std::array<uint8_t,32>& /*downloadChannelId*/,
                             uint64_t                      /*downloadedSize*/,
                             lt::errors::error_code_enum&   outErrorCode ) override
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
        return m_downloadChannelMap[transactionHash].m_requestedSize[peerPublicKey];
    }

    uint64_t receivedSize( const std::array<uint8_t,32>&  transactionHash,
                           const std::array<uint8_t,32>&  peerPublicKey ) override
    {
        return m_downloadChannelMap[transactionHash].m_receivedSize[peerPublicKey];
    }
    
//    virtual void onMessageReceived( const std::string& query, const std::string& message, const boost::asio::ip::udp::endpoint& source ) override
//    {
//    }
    
    void handleDhtResponse( lt::bdecode_node response, boost::asio::ip::udp::endpoint endpoint ) override
    {
        try
        {
            auto rDict = response.dict_find_dict("r");
            auto query = rDict.dict_find_string_value("q");
            
            if ( query == "get_channel_status" )
            {
                if ( m_channelStatusResponseHandler )
                {
                    try
                    {
                        lt::string_view response = rDict.dict_find_string_value("ret");
                        lt::string_view sign = rDict.dict_find_string_value("sign");
                        
                        std::istringstream is( std::string( response.begin(), response.end() ), std::ios::binary );
                        cereal::PortableBinaryInputArchive iarchive(is);

                        ReplicatorKey replicatorKey;
                        ChannelId channelId;
                        iarchive( replicatorKey );
                        iarchive( channelId );

                        // Verify sign
                        //
                        Signature signature;
                        if ( sign.size() != signature.size() )
                        {
                            (*m_channelStatusResponseHandler)( replicatorKey, channelId, {}, "invalid sign size" );
                            __LOG_WARN( "invalid sign size" )
                            return;
                        }
                        memcpy( signature.data(), sign.data(), signature.size() );

                        if ( ! crypto::Verify( replicatorKey, {utils::RawBuffer{ (const uint8_t*)response.begin(), response.size() }}, signature) )
                        {
                            (*m_channelStatusResponseHandler)( replicatorKey, channelId, {}, "invalid sign" );
                            _LOG_WARN( "invalid sign" )
                            return;
                        }

                        if ( rDict.dict_find_string_value("not_found") == "yes" )
                        {
                            (*m_channelStatusResponseHandler)( replicatorKey, channelId, {}, "channel not found" );
                            return;
                        }

                        // parse and accept receipts
                        //
                        for(;;)
                        {
                            DownloadChannelInfo msg;
                            try {
                                iarchive( msg );
                            } catch (...) {
                                return;
                            }

                            (*m_channelStatusResponseHandler)( replicatorKey, channelId, msg, "" );
                        }
                    }
                    catch(...)
                    {
                        (*m_channelStatusResponseHandler)( {}, {}, {}, "bad 'get_channel_status' response" );
                        return;
                    }
                }
            }

            if ( query == "get_modification_status" )
            {
                if ( m_modificationStatusResponseHandler )
                {
                    try
                    {
                        lt::string_view response = rDict.dict_find_string_value("ret");
                        lt::string_view sign = rDict.dict_find_string_value("sign");
                        
                        std::istringstream is( std::string( response.begin(), response.end() ), std::ios::binary );
                        cereal::PortableBinaryInputArchive iarchive(is);

                        ReplicatorKey replicatorKey;
                        Hash256       modificationHash;
                        iarchive( replicatorKey );
                        iarchive( modificationHash );

                        // Verify sign
                        //
                        Signature signature;
                        if ( sign.size() != signature.size() )
                        {
                            (*m_modificationStatusResponseHandler)( replicatorKey, modificationHash, {}, "", false, false, "invalid sign size" );
                            __LOG_WARN( "invalid sign size" )
                            return;
                        }
                        memcpy( signature.data(), sign.data(), signature.size() );

                        if ( ! crypto::Verify( replicatorKey, {utils::RawBuffer{ (const uint8_t*)response.begin(), response.size() }}, signature) )
                        {
                            (*m_modificationStatusResponseHandler)( replicatorKey, modificationHash, {}, "", false, false, "invalid sign" );
                            _LOG_WARN( "invalid sign" )
                            return;
                        }

                        if ( rDict.dict_find_string_value("not_found") == "yes" )
                        {
                            (*m_modificationStatusResponseHandler)( replicatorKey, modificationHash, {}, "", false, false, "not found" );
                            return;
                        }

                        // parse and accept receipts
                        //
                        ModifyTrafficInfo msg;
                        try {
                            iarchive( msg );
                        } catch (...) {
                            (*m_modificationStatusResponseHandler)( {}, {}, {}, "", false, false, "bad 'get_modification_status' response" );
                            return;
                        }
                        auto currentTask = rDict.dict_find_string_value("currentTask");
                        bool isQueued = rDict.dict_find_string_value("taskIsQueued") == "yes";
                        bool isFinished = rDict.dict_find_string_value("taskIsFinished") == "yes";
                        (*m_modificationStatusResponseHandler)( replicatorKey, modificationHash, msg, currentTask, isQueued, isFinished, "" );
                    }
                    catch(...)
                    {
                        (*m_modificationStatusResponseHandler)( {}, {}, {}, "", false, false, "bad 'get_modification_status' response" );
                        return;
                    }
                }
            }
        }
        catch(...)
        {
            __LOG_WARN("Bad DHT response");
        }
    }
    
    static void defaultModificationStatusResponseHandler( const ReplicatorKey& replicatorKey,
                                                   const sirius::Hash256&      modificationHash,
                                                   const ModifyTrafficInfo&    msg,
                                                   lt::string_view             currentTask,
                                                   bool                        isModificationQueued,
                                                   bool                        isModificationFinished,
                                                   const std::string&          error    )
    {
        __LOG( "@@@ -------------------------------------------------------------------" );
        __LOG( "@@@ Modification Status: " << modificationHash );
        __LOG( "@@@  replicatorKey:     " << replicatorKey );
        if ( ! error.empty() )
        {
            __LOG( "@@@  error:          " << error );
        }
        else
        {
            __LOG( "@@@  driveKey:          " << toString(msg.m_driveKey) );
            if ( !currentTask.empty() )
            {
                __LOG( "@@@  currentTask:       " << currentTask );
            }
            if ( isModificationQueued )
            {
                __LOG( "@@@  modification is queued" );
            }
            if ( isModificationFinished )
            {
                __LOG( "@@@  modification is finished" );
            }
            for( auto [hash,sizes]: msg.m_modifyTrafficMap )
            {
                __LOG( "@@@   received:  " << sizes.m_receivedSize << " from: " << toString(hash) );
                __LOG( "@@@    uploaded: " << sizes.m_requestedSize << " to: " << toString(hash) );
            }
        }
        __LOG( "@@@ -------------------------------------------------------------------" );
    }
    
    static void defaultChannelStatusResponseHandler( const ReplicatorKey&         replicatorKey,
                                                     const ChannelId&             channelId,
                                                     const DownloadChannelInfo&   msg,
                                                     const std::string&           error )
    {
        __LOG( "@@@ -------------------------------------------------------------------" );
        __LOG( "@@@ Download Channel Status: " << channelId << " from: " << replicatorKey );
        __LOG( "@@@  driveKey:            " << toString(msg.m_driveKey) );
        __LOG( "@@@  prepaidDownloadSize: " << msg.m_prepaidDownloadSize );
        __LOG( "@@@  totalReceiptsSize:   " << msg.m_totalReceiptsSize );
        
        for( const auto& [clientKey,sizes]: msg.m_sentClientMap )
        {
            __LOG( "@@@   sentSize: " << sizes.m_sentSize << " to client:   " << clientKey );
        }
        for( const auto& [replicatorKey2,uploadInfo]: msg.m_replicatorUploadRequestMap )
        {
            if ( replicatorKey2 == replicatorKey )
            {
                for( const auto& [clientKey,sizes]: uploadInfo.m_clientMap )
                {
                    __LOG( "@@@   acceptedSize/notAccepted: " << sizes.m_acceptedSize << "/" << sizes.m_notAcceptedSize << " replicatorKey:   " << replicatorKey );
                }
            }
        }
        __LOG( "@@@ -------------------------------------------------------------------" );
    }



public:
    void
    onEndpointDiscovered( const std::array<uint8_t, 32>& key, const std::optional<boost::asio::ip::tcp::endpoint>& endpoint ) override {
        m_endpointsManager.updateEndpoint(key, endpoint);
    }

protected:
public: //TODO

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
    boost::asio::post(clientSession->session()->lt_session().get_context(), [clientSession] {
        clientSession->m_endpointsManager.start(clientSession->session());
        clientSession->setEndpointHandler();
    });
    clientSession->addDownloadChannel(Hash256());
    return clientSession;
}

}

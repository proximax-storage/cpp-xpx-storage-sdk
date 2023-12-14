/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "drive/Session.h"
#include "ReplicatorInt.h"
#include "DownloadLimiter.h"
#include "drive/Utils.h"
#include "drive/log.h"

#include <iostream>
#include <vector>
#include <filesystem>
#include <fstream>
#include <cstring>


// boost
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/asio/ip/address.hpp>

// cereal
#include <cereal/types/vector.hpp>
#include <cereal/types/array.hpp>
#include <cereal/types/map.hpp>
#include <cereal/archives/portable_binary.hpp>

// libtorrent
#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/hex.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/torrent.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/extensions/ut_metadata.hpp>
#include <libtorrent/aux_/generate_peer_id.hpp>
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/extensions.hpp"

#include <sirius_drive/session_delegate.h>

#undef DBG_MAIN_THREAD
#define DBG_MAIN_THREAD { assert( m_dbgThreadId == std::this_thread::get_id() ); }

namespace fs = std::filesystem;

namespace sirius::drive {

enum { PIECE_SIZE = 0 };

// Libtorrent "ClientData"
//
struct LtClientData
{
    struct RemoveNotifyer
    {
        RemoveNotifyer( const std::function<void()>& notifyer ) : m_notifyer(notifyer) {}
        ~RemoveNotifyer() { m_notifyer(); }
        std::function<void()> m_notifyer = {};
    };

    enum class FinishStatus {
        TORRENT_ADDED,
        TORRENT_FINISHED,
        TORRENT_FLUSHED
    };
    
    std::shared_ptr<RemoveNotifyer> m_removeNotifyer;

    fs::path                        m_saveFolder          = {};
    fs::path                        m_saveTorrentFilename = {};
    std::vector<DownloadContext>    m_dnContexts          = {};

    uint64_t                        m_downloadLimit       = 0;
    uint64_t                        m_uploadedDataSize    = 0;
    bool                            m_invalidMetadata     = false;
    
    bool                            m_isRemoved           = false;
    Timer                           m_releaseFilesTimer;

    FinishStatus                    m_finishStatus = FinishStatus::TORRENT_ADDED;
};

//
// DefaultSession
//
class DefaultSession:   public Session,
                        public std::enable_shared_from_this<DefaultSession>
{
    std::shared_ptr<kademlia::EndpointCatalogue> m_kademlia;
    
    const int64_t			m_maxTotalSize = 256LL * 1024LL * 1024LL * 1024LL; // 256GB
    
    bool                    m_ownerIsReplicator = true;
    
    std::string             m_addressAndPort;
    int                     m_listeningPort;
    lt::session             m_session;
    
    // It will be called on socket listening error
    LibTorrentErrorHandler  m_alertHandler;
    
    using TorrentDeletedHandler = std::optional<std::function<void( lt::torrent_handle )>>;
    TorrentDeletedHandler m_torrentDeletedHandler;
    
    std::weak_ptr<ReplicatorInt>        m_replicator;
    std::weak_ptr<lt::session_delegate> m_downloadLimiter;
    
    std::string                         m_dbgOurPeerName = "";
    
    bool                                m_stopping = false;
    
    std::thread::id                     m_dbgThreadId;
    
    LogMode                             m_logMode = LogMode::BRIEF;
    
public:
    
    // Constructor for Replicator
    //
    DefaultSession( boost::asio::io_context&             context,
                   std::string                          address,
                   const LibTorrentErrorHandler&        alertHandler,
                   std::weak_ptr<ReplicatorInt>         replicator,
                   std::weak_ptr<lt::session_delegate>  downloadLimiter,
                   const std::vector<ReplicatorInfo>&   bootstraps,
                   std::promise<void>&&                 bootstrapBarrier
                   )
    : m_ownerIsReplicator(true)
    , m_addressAndPort(address)
    , m_listeningPort(extractListeningPort())
    , m_session( lt::session_params{ generateSessionSettings( false, bootstraps) }, context, {})
    , m_alertHandler(alertHandler)
    , m_replicator(replicator)
    , m_downloadLimiter(downloadLimiter)
    {
        boost::asio::post( m_session.get_context(), [=, this]() mutable
        {
            if ( auto replicatorPtr = replicator.lock(); replicatorPtr )
            {
                m_kademlia = createEndpointCatalogue( weak_from_this(), replicatorPtr->keyPair(), bootstraps, uint16_t(m_listeningPort), true );
            }
            else
            {
                _SIRIUS_ASSERT( "Cannot lock replicator" );
            }
            auto plugin = std::make_shared<DhtRequestPlugin>(  m_replicator, weak_from_this() );
            m_session.add_extension(plugin);
            m_session.setDelegate( m_downloadLimiter );
        });
                          
        _LOG( "DefaultSession: " << address << " : " << toString(m_downloadLimiter.lock()->publicKey()) );
        m_dbgOurPeerName = m_downloadLimiter.lock()->dbgOurPeerName();
        
        m_session.set_alert_notify( [this] { this->alertHandler(); } );
        
        _LOG( "DefaultSession created: " );
        _LOG( "DefaultSession created: " << m_addressAndPort );
        _LOG( "DefaultSession created: " << m_addressAndPort << " " << m_replicator.lock() );
        _LOG( "DefaultSession created: " << m_addressAndPort << " " << toString(m_replicator.lock()->replicatorKey().array()) );
        
        _LOG( "DefaultSession created: m_listeningPort " << m_listeningPort );
    }
    
    int extractListeningPort()
    {
        if ( auto colonPos = m_addressAndPort.find(":"); colonPos != std::string::npos )
        {
            std::string portString = m_addressAndPort.substr(colonPos + 1);
            return std::stoi(portString);
        }
        return 0;
    }
    
    virtual int listeningPort() override
    {
        return m_listeningPort;
    }
    
    // Constructor for Client
    //
    DefaultSession( std::string                             address,
                   const crypto::KeyPair&                  keyPair,
                   LibTorrentErrorHandler                  alertHandler,
                   std::weak_ptr<lt::session_delegate>     downloadLimiter,
                   bool                                    useTcpSocket,
                   const std::vector<ReplicatorInfo>&      bootstraps,
                   std::weak_ptr<DhtMessageHandler>        dhtMessageHandler
                   )
    : m_ownerIsReplicator(false)
    , m_addressAndPort(address)
    , m_listeningPort(extractListeningPort())
    , m_session( lt::session_params{ generateSessionSettings( useTcpSocket, bootstraps ) } )
    , m_alertHandler(alertHandler)
    , m_downloadLimiter(downloadLimiter)
    {
        boost::asio::post( m_session.get_context(), [&keyPair,bootstraps,dhtMessageHandler, this]() mutable
        {
            if ( auto downloadLimiterPtr = m_downloadLimiter.lock(); downloadLimiterPtr )
            {
                m_dbgOurPeerName = downloadLimiterPtr->dbgOurPeerName();
                
                m_kademlia = std::move( createEndpointCatalogue( weak_from_this(), keyPair, bootstraps, uint16_t(m_listeningPort), false ));
            }
            auto plugin = std::make_shared<DhtRequestPlugin>(  dhtMessageHandler, weak_from_this() );
            m_session.add_extension(plugin);
            m_session.setDelegate( m_downloadLimiter );
        });
                          
        m_session.set_alert_notify( [this] { this->alertHandler(); } );
        
        _LOG( "DefaultSession created: " << m_addressAndPort );
        _LOG( "Client DefaultSession created: m_listeningPort " << m_listeningPort );
    }
    
    virtual ~DefaultSession()
    {
        //m_session.stop_dht();
        // lt::settings_pack p;
        // p.set_bool(lt::settings_pack::enable_dht, false);
        // m_session.apply_settings(p);
        //m_session.pause();
        
        m_kademlia->stopTimers();
        
        m_stopping = true;
    }
    
    virtual bool      isClient() override
    {
        return m_replicator.expired();
    }

    
    // for dbg
    lt::session &lt_session() override
    {
        return m_session;
    }
    
    virtual void setTorrentDeletedHandler( std::function<void(lt::torrent_handle)> handler ) override
    {
        m_torrentDeletedHandler = handler;
    }
    
    //
    virtual void onTorrentDeleted( lt::torrent_handle handle ) override
    {
        if ( m_torrentDeletedHandler )
        {
            (*m_torrentDeletedHandler)( handle );
        }
        
        if (m_stopping)
        {
            return;
        }
        
        auto userdata = handle.userdata().get<LtClientData>();
        if ( userdata==0 )
        {
            _LOG_WARN( "userdata==0" )
            return;
        }
        
        auto& contextVector = userdata->m_dnContexts;
        
        if ( ! contextVector.empty() && ! userdata->m_invalidMetadata )
        {
            fs::path srcFilePath = fs::path(userdata->m_saveFolder.string() + "/" + hashToFileName( contextVector[0].m_infoHash ));
            //_LOG( "srcFilePath: " << srcFilePath )
            
            for( size_t i=0; i<contextVector.size(); i++ )
            {
                if ( contextVector[i].m_doNotDeleteTorrent )
                {
                    //_LOG( "m_doNotDeleteTorrent: true" )
                    continue;
                }
                
                auto& context = contextVector[i];
                
                //                if ( ! context.m_saveAs.empty() && context.m_downloadType == DownloadContext::file_from_drive )
                //                {
                //                    //_LOG( "context.m_saveAs: " << context.m_saveAs )
                //                    SIRIUS_ASSERT( ! m_ownerIsReplicator )
                //
                //                    fs::path destFilePath = context.m_saveAs;
                //
                //                    std::error_code err;
                //                    if ( !fs::exists( destFilePath.parent_path(), err ) ) {
                //                        fs::create_directories( destFilePath.parent_path(), err );
                //                    }
                //
                //                    //???+++
                //                    if ( fs::exists( destFilePath, err ) ) {
                //                        fs::remove( destFilePath );
                //                    }
                //
                //                    if ( i == contextVector.size()-1 )
                //                    {
                //                        fs::rename( srcFilePath, destFilePath, err );
                //                        if (err)
                //                        {
                //                            _LOG_WARN( "rename err: " << err.message() )
                //                        }
                //                    }
                //                    else
                //                    {
                //                        fs::copy( srcFilePath, destFilePath, err );
                //                    }
                //                }
                
                context.m_downloadNotification( download_status::code::download_complete,
                                               context.m_infoHash,
                                               context.m_saveAs,
                                               userdata->m_uploadedDataSize,
                                               0,
                                               "" );
            }
        }
        
        
        if ( auto userdata = handle.userdata().get<LtClientData>(); userdata )
        {
            //std::thread( [=] {
            //__LOG( "????? use_count: " << userdata->m_removeNotifyer.use_count() << " " << hash )
            delete userdata;
            //}).detach();
        }
    }
    
    void onCacheFlushed( lt::torrent_handle handle ) override
    {
        onTorrentFinished(handle);
    }
    
    lt::settings_pack generateSessionSettings(bool useTcpSocket, const std::vector<ReplicatorInfo>& bootstraps)
    {
        lt::settings_pack settingsPack;
        
        settingsPack.set_int( lt::settings_pack::alert_mask, ~0 );//lt::alert_category::all );
        
        // todo public_key?
        char todoPubKey[32];
        std::memset(todoPubKey,'x', sizeof(todoPubKey));
        todoPubKey[5] = 0;
        settingsPack.set_str(  lt::settings_pack::user_agent, std::string(todoPubKey,32) );
        settingsPack.set_bool( lt::settings_pack::enable_outgoing_utp, true );
        settingsPack.set_bool( lt::settings_pack::enable_incoming_utp, true );
        settingsPack.set_bool( lt::settings_pack::enable_outgoing_tcp, true );
        settingsPack.set_bool( lt::settings_pack::enable_incoming_tcp, true );
        
        //todo 1. is it enough? 2. is it for single peer?
        settingsPack.set_int( lt::settings_pack::dht_upload_rate_limit, 8000000 );
        
        settingsPack.set_bool( lt::settings_pack::enable_dht, true );
        settingsPack.set_bool( lt::settings_pack::enable_lsd, false ); // is it needed?
        settingsPack.set_bool( lt::settings_pack::enable_upnp, true );
        settingsPack.set_bool( lt::settings_pack::enable_natpmp, true );
        
        // We must accept udp messages from 3.*.*.* addresses
        // (maybe we need to edit blocklist in dht_tracker.cpp)
        // (list - 3, 6, 7, 9, 11, 19, 21, 22, 25, 26, 28, 29, 30, 33, 34, 48, 56)
        settingsPack.set_bool( lt::settings_pack::dht_ignore_dark_internet, false );
        
        std::ostringstream bootstrapsBuilder;
        for ( const auto& bootstrap: bootstraps )
        {
            bootstrapsBuilder << bootstrap.m_endpoint.address().to_string() << ":" << std::to_string(bootstrap.m_endpoint.port()) << ",";
        }
        
        std::string bootstrapList = bootstrapsBuilder.str();
        
        settingsPack.set_str(  lt::settings_pack::dht_bootstrap_nodes, bootstrapList);
        
        settingsPack.set_str(  lt::settings_pack::listen_interfaces, m_addressAndPort );
        settingsPack.set_bool( lt::settings_pack::allow_multiple_connections_per_ip, true );
        settingsPack.set_bool( lt::settings_pack::enable_ip_notifier, false );
        
        settingsPack.set_int( lt::settings_pack::max_retry_port_bind, 0 );
        settingsPack.set_bool( lt::settings_pack::listen_system_port_fallback, false );
        
        //settingsPack.set_int( lt::settings_pack::max_out_request_queue, 10 );
        
        return settingsPack;
    }
    
    
    void setDbgThreadId()
    {
        m_dbgThreadId = std::this_thread::get_id();
    }
    
    virtual void endSession() override {
        m_stopping = true;
        _LOG( "stop session" )
    }
    
    virtual bool isEnding() override {
        return m_stopping;
    }
    
    virtual void      setEndpointHandler( EndpointHandler endpointHandler ) override
    {
        m_kademlia->setEndpointHandler( endpointHandler );
    }

    virtual void      startSearchPeerEndpoints( const std::vector<Key>& keys ) override
    {
        for( const auto& key : keys )
        {
            m_kademlia->getEndpoint( key );
        }
    }

    virtual void      addClientToLocalEndpointMap( const Key& key ) override
    {
        m_kademlia->addClientToLocalEndpointMap( key );
    }

    virtual void      onEndpointDiscovered( const Key& key, const std::optional<boost::asio::ip::udp::endpoint>& endpoint ) override
    {
        m_kademlia->onEndpointDiscovered( key, endpoint );
    }

    virtual std::optional<boost::asio::ip::udp::endpoint> getEndpoint( const Key& key ) override
    {
        return getEndpoint( key );
    }
    
    virtual void      addReplicatorKeyToKademlia( const Key& key ) override
    {
        m_kademlia->addReplicatorKey(key);
    }
    
    virtual void      addReplicatorKeysToKademlia( const std::vector<Key>& keys ) override
    {
        m_kademlia->addReplicatorKeys(keys);
    }
    
    virtual void      removeReplicatorKeyFromKademlia( const Key& keys ) override
    {
        m_kademlia->removeReplicatorKey(keys);
    }
    
    virtual void     dbgTestKademlia( KademliaDbgFunc dbgFunc ) override
    {
        m_kademlia->dbgTestKademlia(dbgFunc);
    }


    virtual void removeTorrentsFromSession( const std::set<lt::torrent_handle>&  torrents,
                                           std::function<void()>                endNotification,
                                           bool removeFiles ) override
    {
        auto toBeRemoved = std::set<lt::torrent_handle>();
        
        //_LOG( "+++ ex *** removeTorrentsFromSession: " << torrents.size() << " " << " get_torrents().size()=" << m_session.get_torrents().size() )
        
        for( const auto& torrentHandle : torrents )
        {
            //            _LOG("remove_torrent(2): " << torrentHandle.info_hashes().v2 << " " << torrentHandle.status().state )
            _LOG("remove_torrent(2): " << torrentHandle.info_hashes().v2 )
            if ( !torrentHandle.is_valid() )
            {
                //(???++++)
                _LOG_WARN("TRY REMOVE NOT VALID TORRENT");
            }
            else
            {
                if ( torrentHandle.status().state > 2 ) // torrentHandle.status().state == lt::torrent_status::seeding )
                {
                    toBeRemoved.insert(torrentHandle);
                }
                torrentHandle.userdata().get<LtClientData>()->m_isRemoved = true;
            }
        }
        
        _LOG("remove_torrent(2a): ")
        
        if ( !toBeRemoved.empty() )
        {
            //m_removeContexts.push_back( std::make_unique<RemoveTorrentContext>( toBeRemoved, endNotification ) );
            
            auto removeNotifyer = std::make_shared<LtClientData::RemoveNotifyer>(endNotification);
            
            for( const auto& torrentHandle : torrents )
            {
                if ( torrentHandle.is_valid() && torrentHandle.status().state > 2 )
                {
                    _SIRIUS_ASSERT( torrentHandle.userdata().get<LtClientData>() != nullptr )
                    _SIRIUS_ASSERT( ! torrentHandle.userdata().get<LtClientData>()->m_removeNotifyer )
                    torrentHandle.userdata().get<LtClientData>()->m_removeNotifyer = removeNotifyer;
                }
            }
            
            // races!
            removeNotifyer.reset();
            
            for( const auto& torrentHandle : torrents )
            {
                //                if ( torrentHandle.userdata().get<LtClientData>()->m_removeNotifyer )
                //                {
                if ( torrentHandle.is_valid() && torrentHandle.status().state > 2 )
                {
                    _LOG( "+++ ex :remove_torrent(3): " << torrentHandle.info_hashes().v2 );
                    lt::remove_flags_t removeFlag = removeFiles ? lt::session::delete_files : lt::session::delete_partfile;
                    m_session.remove_torrent( torrentHandle, removeFlag );
                }
                //                }
            }
        }
        else
        {
            for( const auto& torrentHandle : torrents )
            {
                //                if ( torrentHandle.status().state > 2 )
                //                {
                m_session.remove_torrent( torrentHandle, lt::session::delete_partfile );
                //                }
            }
            
            boost::asio::post(lt_session().get_context(), [=] {
                endNotification();
            });
        }
    }
    
    virtual lt_handle addTorrentFileToSession( const std::filesystem::path&     torrentFilename,
                                              const std::filesystem::path&     folderWhereFileIsLocated,
                                              lt::SiriusFlags::type            siriusFlags,
                                              const std::array<uint8_t,32>*    driveKey,
                                              const std::array<uint8_t,32>*    channelId,
                                              const std::array<uint8_t,32>*    modifyTx,
                                              endpoint_list list,
                                              uint64_t* outTotalSize ) override
    {
        // create add_torrent_params
        lt::add_torrent_params params;
        params.userdata = new LtClientData();
        params.flags &= ~lt::torrent_flags::paused;
        params.flags &= ~lt::torrent_flags::auto_managed;
        
        //(???+++)
        params.flags &= ~lt::torrent_flags::update_subscribe;
        params.flags &= ~lt::torrent_flags::apply_ip_filter;
        params.flags &= ~lt::torrent_flags::need_save_resume;
        
        params.flags |= lt::torrent_flags::seed_mode;
        params.flags |= lt::torrent_flags::upload_mode;
        params.flags |= lt::torrent_flags::no_verify_files;
        
        // set super seeding mode for clients
        //        if ( !(siriusFlags & lt::sf_is_replicator) )
        //        {
        //            params.flags |= lt::torrent_flags::super_seeding;
        //        }
        
        params.storage_mode     = lt::storage_mode_sparse;
        params.save_path        = folderWhereFileIsLocated.string();
        
        lt::error_code tInfoErrorCode;
        params.ti = std::make_shared<lt::torrent_info>( torrentFilename.string(), tInfoErrorCode );
        if ( tInfoErrorCode )
        {
            _LOG_WARN( "session::addTorrentFileToSession error: " << tInfoErrorCode.message() << " code: " << tInfoErrorCode.value() )
            return {};
        }
        
        params.m_siriusFlags    = siriusFlags;
        if ( driveKey )
            params.m_driveKey = *driveKey;
        if ( channelId )
            params.m_channelId = *channelId;
        if ( modifyTx )
            params.m_modifyTx = *modifyTx;
        
        auto hash = params.ti->info_hashes().get_best();
        
        //dbg///////////////////////////////////////////////////
        //        auto tInfo = lt::torrent_info(buffer, lt::from_span);
        //        _LOG( "addTorrentToSession: " << torrentFilename << "; infoHash:" << tInfo.info_hashes().v2 );
        
        //        LOG( tInfo.info_hashes().v2 ) );
        //        LOG( "add torrent: torrent filename:" << torrentFilename );
        //        LOG( "add torrent: fileFolder:" << fileFolder );
        //LOG( "add torrent: " << lt::make_magnet_uri(tInfo) );
        //dbg///////////////////////////////////////////////////
        
        lt::error_code ec;
        lt::torrent_handle tHandle = m_session.add_torrent( std::move(params), ec );
        if ( ec )
        {
            _LOG_WARN( "session::add_torrent error: ec: " << ec );
        }
        
        connectPeers( tHandle, list );
        
        if ( outTotalSize != nullptr )
        {
            *outTotalSize = tHandle.torrent_file()->total_size();
            _LOG("Out total size " << hash << " " << *outTotalSize)
        }
        
        return tHandle;
    }
    
    // downloadFile
    virtual lt_handle download( DownloadContext&&               downloadContext,
                               const std::filesystem::path&    saveFolder,
                               const std::filesystem::path&    saveTorrentFilePath,
                               const ReplicatorList&           keysHints,
                               const std::array<uint8_t,32>*   driveKey  = nullptr,
                               const std::array<uint8_t,32>*   channelId = nullptr,
                               const std::array<uint8_t,32>*   modifyTx  = nullptr,
                               const endpoint_list&            endpointsHints = {} ) override
    {
        _LOG( "Session::download: dnCtxt.m_infoHash:   " << downloadContext.m_infoHash )
        _LOG( "Session::download: dnCtxt.m_saveAs:     " << downloadContext.m_saveAs )
        _LOG( "Session::download: saveFolder:          " << saveFolder )
        _LOG( "Session::download: saveTorrentFilePath: " << saveTorrentFilePath )
        
        // create add_torrent_params
        lt::error_code ec;
        lt::add_torrent_params params = lt::parse_magnet_uri( magnetLink(downloadContext.m_infoHash), ec );
        
        if (ec) {
            throw std::runtime_error( std::string("downloadFile error: ") + ec.message() );
        }
        
        auto userdata = new LtClientData();
        userdata->m_saveTorrentFilename = fs::path(saveTorrentFilePath).make_preferred();
        userdata->m_saveFolder          = fs::path(saveFolder).make_preferred();
        params.userdata = userdata;
        params.flags &= ~lt::torrent_flags::paused;
        params.flags &= ~lt::torrent_flags::auto_managed;
        
        // where the file will be placed
        params.save_path = saveFolder.string();
        
        if ( driveKey )
            params.m_driveKey = *driveKey;
        if ( channelId )
            params.m_channelId = *channelId;
        if ( modifyTx )
            params.m_modifyTx = *modifyTx;
        
        if ( downloadContext.m_downloadType == DownloadContext::client_data || downloadContext.m_downloadType == DownloadContext::missing_files )
            params.m_siriusFlags     = lt::SiriusFlags::peer_is_replicator | lt::SiriusFlags::replicator_is_receiver;
        else
            params.m_siriusFlags     = lt::SiriusFlags::client_is_receiver;
        
        // create torrent_handle
        lt::torrent_handle tHandle = m_session.add_torrent(params,ec);
        if (ec) {
            _LOG( "downloadFile error: " << ec.message() << " " << magnetLink(downloadContext.m_infoHash) );
            throw std::runtime_error( std::string("downloadFile error: ") + ec.message() );
        }
        
        if ( !m_session.is_valid() )
            throw std::runtime_error("downloadFile: libtorrent session is not valid");
        
        if ( !tHandle.is_valid() )
            throw std::runtime_error("downloadFile: torrent handle is not valid");
        
        // connect to peers
        if ( auto limiter = m_downloadLimiter.lock(); limiter )
        {
            for( const auto& key : keysHints ) {
                auto endpoint = limiter->getEndpoint( key.array() );
                if ( endpoint )
                {
                    _LOG( "connect_peer: " << *endpoint << " " << key );
                    tHandle.connect_peer( boost::asio::ip::tcp::endpoint{ endpoint->address(), endpoint->port() } );
                }
            }
        }
        
        for ( const auto& endpoint: endpointsHints )
        {
            tHandle.connect_peer( boost::asio::ip::tcp::endpoint{ endpoint.address(), endpoint.port() } );
        }
        
        // set fs tree save path
        if ( downloadContext.m_downloadType == DownloadContext::fs_tree ) {
            downloadContext.m_saveAs = (saveFolder / FS_TREE_FILE_NAME).make_preferred();
        }
        
        _SIRIUS_ASSERT( tHandle.userdata().get<LtClientData>() != nullptr )
        tHandle.userdata().get<LtClientData>()->m_dnContexts.push_back( downloadContext );
        
        return tHandle;
    }
    
    
    void removeDownloadContext( lt::torrent_handle tHandle ) override
    {
    }
    
    void connectPeers( lt::torrent_handle tHandle, endpoint_list list ) {
        
        if ( !m_session.is_valid() )
            throw std::runtime_error("connectPeers: libtorrent session is not valid");
        
        //TODO check if not set m_lastTorrentFileHandle
        for( const auto& endpoint : list ) {
            _LOG( "connectPeers: " << endpoint )
            tHandle.connect_peer(boost::asio::ip::tcp::endpoint{ endpoint.address(), endpoint.port() });
        }
    }
    
    void connectTorentsToEndpoint( const boost::asio::ip::udp::endpoint& endpoint ) override
    {
        std::vector<lt::torrent_handle> torrents = m_session.get_torrents();
        for( const lt::torrent_handle& tHandle : torrents )
        {
            //if ( tHandle.in_session() )
            if ( tHandle.is_valid() )
            {
                tHandle.connect_peer( boost::asio::ip::tcp::endpoint{endpoint.address(),endpoint.port()});
            }
        }
    }
    void      dbgPrintActiveTorrents() override
    {
        _LOG( "Active torrents:" );
        std::vector<lt::torrent_handle> torrents = m_session.get_torrents();
        for( const lt::torrent_handle& tHandle : torrents )
        {
            //            if ( tHandle.is_valid() )
            if ( tHandle.in_session() )
            {
                auto status = tHandle.status( lt::torrent_handle::query_save_path | lt::torrent_handle::query_name );
                _LOG( " file hash: " << tHandle.info_hashes().v2 << " file:" << status.save_path << "/" << status.name );
            }
        }
    }
    
#ifdef __APPLE__
#pragma mark --messaging--
#endif
    
    struct DhtRequestPlugin : lt::plugin
    {
        std::weak_ptr<DhtMessageHandler>    m_handler;
        std::weak_ptr<Session>              m_session;
        
        DhtRequestPlugin( std::weak_ptr<DhtMessageHandler> replicator, std::weak_ptr<Session> kademliaTransport )
            : m_handler(replicator), m_session(kademliaTransport) {}
        
        feature_flags_t implemented_features() override
        {
            return plugin::dht_request_feature;
        }
        
        bool on_dht_request(
                            lt::string_view                         query,
                            boost::asio::ip::udp::endpoint const&   source,
                            lt::bdecode_node const&                 message,
                            lt::entry&                              response ) override
        {
            try
            {
                if ( query == "get_peers" || query == "announce_peer" )
                {
                    return false;
                }
                
                if ( query == GET_MY_IP_MSG ) // Our Kademlia message
                {
                    auto str = message.dict_find_string_value( "x" );
                    std::string packet((char*) str.data(), (char*) str.data() + str.size());

                    if ( auto session = m_session.lock(); session )
                    {
                        std::string kademliaResponse = session->onGetMyIpRequest( packet, source );
                        
                        if ( ! kademliaResponse.empty() )
                        {
                            session->sendMessage( MY_IP_RESPONSE, source, kademliaResponse );
                        }
                    }
                    return true;
                }
                else if ( query == GET_PEER_IP_MSG ) // Our Kademlia message
                {
                    auto str = message.dict_find_string_value( "x" );
                    std::string packet((char*) str.data(), (char*) str.data() + str.size());

                    if ( auto session = m_session.lock(); session )
                    {
                        std::string kademliaResponse = session->onGetPeerIpRequest( packet, source );
                        
                        if ( ! kademliaResponse.empty() )
                        {
                            session->sendMessage( PEER_IP_RESPONSE, source, kademliaResponse );
                        }
                    }
                    return true;
                }
                else if ( query == MY_IP_RESPONSE ) // Our Kademlia message
                {
                    auto str = message.dict_find_string_value( "x" );
                    std::string packet((char*) str.data(), (char*) str.data() + str.size());

                    if ( auto session = m_session.lock(); session )
                    {
                        session->onGetMyIpResponse( packet, source );
                    }
                    return true;
                }
                else if ( query == PEER_IP_RESPONSE ) // Our Kademlia message
                {
                    auto str = message.dict_find_string_value( "x" );
                    std::string packet((char*) str.data(), (char*) str.data() + str.size());

                    if ( auto session = m_session.lock(); session )
                    {
                        session->onGetPeerIpResponse( packet, source );
                    }
                    return true;
                }
                else if ( auto handler = m_handler.lock(); handler )
                {
                    return handler->on_dht_request( query,
                                                   source,
                                                   message,
                                                   response );
                }
            }
            catch( std::runtime_error& ex )
            {
                __LOG( "!!!ERROR!!!: unhandled exception: " << ex.what() );
            }
            catch(...)
            {
                __LOG( "!!!ERROR!!!: unhandled exception: in on_dht_request()" );
            }
            
            return false;
        }
    };
    
    //    void  sendMessage( boost::asio::ip::udp::endpoint udp, const std::vector<uint8_t>& ) override
    //    {
    //        lt::entry e;
    //        e["q"] = "sirius_message";
    //        e["x"] = "-----------------------------------------------------------";
    //        LOG( "lt::entry e: " << e );
    //
    ////        lt::client_data_t client_data(this);
    //        m_session.dht_direct_request( udp, e, lt::client_data_t(reinterpret_cast<int*>(12345))  );
    //    }
    
    void sendMessage( const std::string&                query,
                     boost::asio::ip::udp::endpoint    endPoint,
                     const std::vector<uint8_t>&       message,
                     const Signature*                  signature ) override
    {
        _LOG( "signature sendMessage: query: " << query << " " << endPoint )
        
        lt::entry entry;
        entry["q"] = query;
        entry["x"] = std::string( message.begin(), message.end() );
        
        if ( signature != nullptr )
        {
            entry["sign"] = std::string( signature->begin(), signature->end() );
        }
        
        m_session.dht_direct_request( endPoint, entry );//, lt::client_data_t(userdata) );
    }
    
    void sendMessage( const std::string& query, boost::asio::ip::udp::endpoint endPoint, const std::string& message ) override
    {
        _LOG( "sendMessage: query: " << query << " " << endPoint )
        
        lt::entry entry;
        entry["q"] = query;
        entry["x"] = message;
        
        m_session.dht_direct_request( endPoint, entry );
    }
    
    void handleDhtResponse( lt::bdecode_node response, boost::asio::ip::udp::endpoint endpoint )
    {
        if ( auto delegate = m_downloadLimiter.lock(); delegate )
        {
            // ??? (not stable)
            delegate->handleDhtResponse( response, endpoint );
        }
    }
    
    virtual Timer startTimer( int milliseconds, std::function<void()> func ) override
    {
        auto delegate = m_downloadLimiter.lock();
        if ( !delegate || delegate->isStopped() )
        {
            return {};
        }
        
        return { m_session.get_context(), milliseconds, std::move( func ) };
    }
    
    void setLogMode( LogMode mode ) override
    {
        _LOG( "Set Log Mode: " << static_cast<uint8_t>(mode) );
        m_logMode = mode;
    }
    
public:
    boost::asio::io_context& getContext() override
    {
        return m_session.get_context();
    }
    
private:
    
    void processEndpointItem(lt::dht_mutable_item_alert* theAlert)
    {
        if ( theAlert->seq < 0 || theAlert->seq > std::numeric_limits<int64_t>::max() )
        {
            return;
        }
        
        auto limiter = m_downloadLimiter.lock();
        if ( !limiter )
        {
            return;
        }
        
        auto response = theAlert->item.string();
        
        //        boost::asio::ip::address ipAddress = boost::asio::ip::make_address("127.0.0.1"); // Example IP address
        //        unsigned short portNumber = 12345; // Example port number
        //        boost::asio::ip::udp::endpoint ep(ipAddress, portNumber);
        //        response = ep.address().to_string() + "?" + std::to_string( ep.port() );
        
        //auto endpoint = *reinterpret_cast<boost::asio::ip::udp::endpoint*>(response.data());
        std::vector<std::string> addressAndPort;
        boost::split( addressAndPort, response, [](char c){ return c=='?'; } );
        if (addressAndPort.size() != 2)
        {
            _LOG( "!!!ERROR!!! Bad endpoint data" )
            return;
        }
        
        boost::system::error_code ec;
        auto addr = boost::asio::ip::make_address(addressAndPort[0],ec);
        if (ec)
        {
            _LOG( "!!!ERROR!!! DefaultSession::processEndpointItem: " << ec.message() << " code: " << ec.value() )
            return;
        }
        
        boost::asio::ip::udp::endpoint endpoint{ addr, (uint16_t)std::stoi( addressAndPort[1] ) };
        
        auto publicKey = *reinterpret_cast<std::array<uint8_t, 32> *>( &theAlert->key );
        _LOG( "DefaultSession::processEndpointItem: " << toString(publicKey) << " endpoint: " << endpoint.address().to_string() << " : " << endpoint.port() )
        limiter->onEndpointDiscovered(publicKey, endpoint);
    }
    
    void saveTorrentFile( const std::shared_ptr<lt::torrent_info>&& ti, LtClientData* userdata )
    {
        lt::create_torrent ct(*ti);
        lt::entry te = ct.generate();
        std::vector<char> buffer;
        bencode(std::back_inserter(buffer), te);
        
        if ( FILE* f = fopen( userdata->m_saveTorrentFilename.string().c_str(), "wb+" ); f )
        {
            fwrite( &buffer[0], 1, buffer.size(), f );
            fclose(f);
        }
        
        //                                    userdata->m_saveTorrentFilename = {};
        
        auto dnContext = userdata->m_dnContexts.front();
        if ( dnContext.m_doNotDeleteTorrent )
        {
            // Notify about finishing
            boost::asio::post( lt_session().get_context(), [=]
                              {
                dnContext.m_downloadNotification( download_status::code::download_complete,
                                                 dnContext.m_infoHash,
                                                 dnContext.m_saveAs,
                                                 userdata->m_uploadedDataSize,
                                                 0,
                                                 "" );
            });
        }
    }
    
    void onTorrentFinished( const lt::torrent_handle& handle )
    {
        _LOG( "*** torrent_finished_alert: " << handle.info_hashes().v2 );
        _LOG( "***                   file: " << handle.torrent_file()->files().file_path(0) );
        _LOG( "***              save_path: " << handle.status(lt::torrent_handle::query_save_path).save_path );
        
        auto userdata = handle.userdata().get<LtClientData>();
        
        if ( userdata == nullptr )
        {
            return;
        }
        
        if ( userdata->m_isRemoved )
        {
            return;
        }
        
        SIRIUS_ASSERT(userdata->m_finishStatus == LtClientData::FinishStatus::TORRENT_FINISHED);
        userdata->m_finishStatus = LtClientData::FinishStatus::TORRENT_FLUSHED;
        
        if ( userdata->m_dnContexts.size() > 0 )
        {
            auto dnContext = userdata->m_dnContexts.front();
            if ( dnContext.m_doNotDeleteTorrent )
            {
                if ( userdata->m_saveTorrentFilename.empty())
                {
                    //                                boost::asio::post( lt_session().get_context(), [=]
                    //                                {
                    dnContext.m_downloadNotification( download_status::code::download_complete,
                                                     dnContext.m_infoHash,
                                                     dnContext.m_saveAs,
                                                     userdata->m_uploadedDataSize,
                                                     0,
                                                     "" );
                    //                                });
                } else
                {
                    auto ti = handle.torrent_file_with_hashes();
                    
                    // Save torrent file
                    if ( auto replicator = m_replicator.lock(); replicator )
                    {
                        replicator->executeOnBackgroundThread( [ti = std::move( ti ), userdata, this]
                                                              {
                            saveTorrentFile( std::move( ti ), userdata );
                        } );
                    } else
                    {
                        // Client could save torrent-file on main thread
                        saveTorrentFile( std::move( ti ), userdata );
                    }
                }
            } else
            {
                _LOG( "***                removed: " << handle.info_hashes().v2 );
                m_session.remove_torrent( handle, lt::session::delete_partfile );
            }
        }
    }
    
    void alertHandler()
    {
        //DBG_MAIN_THREAD
        
        if ( m_stopping )
        {
            return;
        }
        
        try
        {
            
            // extract alerts
            std::vector<lt::alert *> alerts;
            m_session.pop_alerts(&alerts);
            
            // loop by alerts
            for (auto &alert : alerts) {
                
                if (m_logMode == LogMode::FULL)
                {
                    _LOG( ">>>" << alert->what() << " (type="<< alert->type() <<"):  " << alert->message() );
                }
                
                ////            if ( alert->type() == lt::dht_log_alert::alert_type || alert->type() == lt::dht_direct_response_alert::alert_type )
                //            {
                //                    _LOG( ">" << m_addressAndPort << " " << alert->what() << ":("<< alert->type() <<")  " << alert->message() );
                //            }
                
                //            if ( alert->type() != lt::log_alert::alert_type )
                //            {
                ////                if ( m_addressAndPort == "192.168.1.102:5551" ) {
                //                    LOG( ">" << m_addressAndPort << " " << alert->what() << ":("<< alert->type() <<")  " << alert->message() );
                ////                }
                //            }
                
#ifdef __APPLE__
#pragma mark --alerts--
#endif
                //_LOG( alert->message() );
                
                switch (alert->type()) {
                        
                        //todo++++
                        //                case lt::torrent_log_alert::alert_type:
                        //                {
                        //                    auto* theAlert = dynamic_cast<lt::torrent_log_alert*>(alert);
                        //                    _LOG( theAlert->message() );
                        //                    break;
                        //                }
                        
                        //                    case lt::log_alert::alert_type: {
                        //                        ___LOG(  m_listeningPort << " : log_alert: " << alert->message())
                        //                        break;
                        //                    }
                                                
                    case lt::peer_log_alert::alert_type: {
                        if ( m_logMode == LogMode::PEER )
                        {
                            _LOG(  ": peer_log_alert: " << alert->message())
                        }
                        break;
                    }
                        
                    case lt::listen_failed_alert::alert_type: {
                        this->m_alertHandler( alert );
                        
                        auto *theAlert = dynamic_cast<lt::listen_failed_alert *>(alert);
                        
                        if ( theAlert ) {
                            LOG(  "listen error: " << theAlert->message())
                        }
                        break;
                    }
                        
//                    case lt::dht_bootstrap_alert::alert_type: {
//                        ___LOG( m_listeningPort << " : dht_bootstrap_alert: " << alert->message() )
//
//                        //m_kademlia->start();
//                        break;
//                    }
                        
                    case lt::external_ip_alert::alert_type: {
                        auto* theAlert = dynamic_cast<lt::external_ip_alert*>(alert);
                        _LOG( "External Ip Alert " << " " << theAlert->message())
                        break;
                    }
                        
                    case lt::dht_announce_alert::alert_type: {
                        break;
                    }
                        
                    case lt::dht_immutable_item_alert::alert_type: {
                        //_LOG( "*** lt::dht_immutable_item_alert::alert_type: " );
                        break;
                    }
                        
                    case lt::dht_mutable_item_alert::alert_type: {
                        
                        auto* theAlert = dynamic_cast<lt::dht_mutable_item_alert*>(alert);
                        if ( theAlert->salt == "epdbg" )
                        {
                            processEndpointItem( theAlert );
                        }
                        
                        break;
                    }
                        
                    case lt::add_torrent_alert::        alert_type:
                    {
                        auto* theAlert = dynamic_cast<lt::add_torrent_alert*>(alert);
                        _LOG( "*** add_torrent_alert: " << theAlert->handle.info_hashes().v2 );
                        //_LOG( "*** added get_torrents().size()=" << m_session.get_torrents().size() );
                        break;
                    }
                        
#ifdef __APPLE__
#pragma mark --metadata_received_alert
#endif
                    case lt::metadata_received_alert::        alert_type:
                    {
                        //sleep(1);
                        auto* theAlert = dynamic_cast<lt::metadata_received_alert*>(alert);
                        if ( theAlert->handle.is_valid() && theAlert->handle.userdata().get<LtClientData>() != nullptr )
                        {
                            auto userdata = theAlert->handle.userdata().get<LtClientData>();
                            
                            std::optional<std::string> errorText;
                            
                            auto torrentInfo = theAlert->handle.torrent_file();
                            
                            if (torrentInfo->total_size() > m_maxTotalSize) {
                                errorText = "Max Total Size Exceeded";
                                _LOG( "+**** Max Total Size Exceeded: " << torrentInfo->total_size() );
                            }
                            
                            if ( !errorText )
                            {
                                auto expectedPieceSize = lt::create_torrent::automatic_piece_size(torrentInfo->total_size());
                                auto actualPieceSize = torrentInfo->piece_length();
                                
                                if (expectedPieceSize != actualPieceSize) {
                                    errorText = "Invalid Piece Size";
                                    _LOG( "+**** Invalid Piece Size: " << actualPieceSize << " " << expectedPieceSize );
                                }
                            }
                            
                            if ( !errorText )
                            {
                                int64_t downloadLimit = (userdata->m_dnContexts.size() == 0) ? 0 : userdata->m_dnContexts.front().m_downloadLimit;
                                userdata->m_uploadedDataSize = theAlert->handle.torrent_file()->total_size();
                                
                                if ( downloadLimit != 0 && downloadLimit < torrentInfo->total_size() ) {
                                    errorText = "Limit Is Exceeded";
                                    _LOG( "+**** limitIsExceeded: " << torrentInfo->total_size() );
                                }
                            }
                            
                            if ( errorText )
                            {
                                m_session.remove_torrent( theAlert->handle, lt::session::delete_files );
                                
                                userdata->m_invalidMetadata = true;
                                SIRIUS_ASSERT( userdata->m_dnContexts.size()==1 )
                                userdata->m_dnContexts.front().m_downloadNotification(
                                                                                      download_status::code::dn_failed,
                                                                                      userdata->m_dnContexts.front().m_infoHash,
                                                                                      userdata->m_dnContexts.front().m_saveAs,
                                                                                      userdata->m_uploadedDataSize,
                                                                                      0,
                                                                                      *errorText);
                            }
                        }
                        
                        break;
                    }
                        
                        //                case lt::dht_announce_alert::       alert_type:
                        //                case lt::torrent_log_alert::        alert_type:
                        //                case lt::incoming_connection_alert::alert_type: {
                        //                    LOG( m_addressAndPort << " " << alert->what() << ":("<< alert->type() <<")  " << alert->message() );
                        //                    break;
                        //                }
                        
#ifdef __APPLE__
#pragma mark --dht_direct_response_alert
#endif
                    case lt::dht_direct_response_alert::alert_type: {
                        if ( m_stopping )
                        {
                            break;
                        }
                        
                        auto* theAlert = dynamic_cast<lt::dht_direct_response_alert*>(alert);
                        //_LOG( "*** dht_direct_response_alert: " );
                        auto response = theAlert->response();
                        if ( response.type() == lt::bdecode_node::dict_t )
                        {
                            auto rDict = response.dict_find_dict("r");
                            if ( rDict.type() == lt::bdecode_node::dict_t )
                            {
                                auto query = rDict.dict_find_string_value("q");
                                //                             if ( query.size() > 0 && query != "chunk-info" && query != "endpoint_request"
                                //                                    && query != "handshake" && query != "endpoint_response" )
                                //                             {
                                //                                 _LOG( "dht_query: " << query );
                                //                                 _LOG( "" );
                                //                             }
                                //_LOG( "dht_query: " << query )
                                if ( query == "get_dn_rcpts" || query == "get-chunks-info" || query == "get-playlist-hash" ||
                                    query == "get_channel_status" || query == "get_modification_status" || query == "get_stream_status" )
                                {
                                    handleDhtResponse( response, theAlert->endpoint );
                                }
                            }
                        }
                        else
                        {
                            //_LOG_WARN( "*** NULL dht_direct_response_alert: " << theAlert->what() << ":("<< alert->type() <<")  " << theAlert->message() );
                            _LOG( "*** NULL dht_direct_response_alert: " << theAlert->what() << ":("<< alert->type() <<")  " << theAlert->message() );
                        }
                        break;
                    }
                        
                    case lt::incoming_request_alert::alert_type: {
                        //                    auto* theAlert = dynamic_cast<lt::incoming_request_alert*>(alert);
                        //
                        //                    LOG( m_addressAndPort << " " << "#!!!incoming_request_alert!!!: " << theAlert->endpoint <<
                        //                        " " << theAlert->pid << " " << theAlert->req.length << "\n" );
                        //                    LOG( "# : " << theAlert->torrent_name() <<
                        //                        " " << theAlert->req.piece << " " << theAlert->req.start << " " << theAlert->req.length << "\n" );
                        break;
                    }
                        
                        
                    case lt::block_downloading_alert::alert_type: {
                        //                    auto* theAlert = dynamic_cast<lt::block_downloading_alert*>(alert);
                        //
                        //                    LOG( m_addressAndPort << " " << "#!!!block_downloading_alert!!!: " << theAlert->endpoint << "\n" );
                        //                    LOG( "#!!!block_downloading_alert!!!: block idx:" << theAlert->block_index << " piece_index:" << theAlert->piece_index << " pid:" << theAlert->pid << "\n" );
                        break;
                    }
                        
                    case lt::peer_snubbed_alert::alert_type: {
                        auto* theAlert = dynamic_cast<lt::peer_snubbed_alert*>(alert);
                        
                        _LOG( "#!!!peer_snubbed_alert!!!: " << theAlert->endpoint << "\n" );
                        break;
                    }
                        
                    case lt::peer_disconnected_alert::alert_type: {
                        //                    auto* theAlert = dynamic_cast<lt::peer_disconnected_alert*>(alert);
                        //
                        //                    LOG( "#peer_disconnected_alert: " << theAlert->error.category().name() << " " << theAlert->error << " " << theAlert->endpoint << "\n" );
                        //                    break;
                    }
                        
#ifdef __APPLE__
#pragma mark --download-progress--
#endif
                        // piece_finished_alert
                    case lt::piece_finished_alert::alert_type:
                    {
                        //                    auto *theAlert = dynamic_cast<lt::piece_finished_alert *>(alert);
                        //
                        //                    if ( theAlert ) {
                        //
                        //                        _LOG( "@@@ piece_finished_alert: " << theAlert->handle.torrent_file()->files().file_path(0) );
                        //
                        //                        // TODO: better to use piece_granularity
                        //                        std::vector<int64_t> fp = theAlert->handle.file_progress();// lt::torrent_handle::piece_granularity );
                        //
                        //                        bool calculatePercents = false;//true;
                        //                        uint64_t dnBytes = 0;
                        //                        uint64_t totalBytes = 0;
                        //
                        //                        // check completeness
                        //                        bool isAllComplete = true;
                        //                        for( uint32_t i=0; i<fp.size(); i++ ) {
                        //
                        //                            auto fsize = theAlert->handle.torrent_file()->files().file_size(i);
                        //                            bool const complete = ( fp[i] == fsize );
                        //
                        //                            isAllComplete = isAllComplete && complete;
                        //
                        //                            if ( calculatePercents )
                        //                            {
                        //                                dnBytes    += fp[i];
                        //                                totalBytes += fsize;
                        //                            }
                        //
                        //                            //dbg/////////////////////////
                        //                            const std::string filePath = theAlert->handle.torrent_file()->files().file_path(i);
                        //                            _LOG( "@@@ progress: " << fp[i] << " of " << fsize << " " << filePath );
                        //                            //dbg/////////////////////////
                        //                        }
                        //
                        //                        if ( calculatePercents )
                        //                        {
                        //                            _LOG( "@@@  progress: " << 100.*double(dnBytes)/double(totalBytes) << "   " << dnBytes << "/" << totalBytes << "  piece_index=" << theAlert->piece_index );
                        //                        }
                        //
                        //                        if ( isAllComplete )
                        //                        {
                        //                            _LOG( "@@@ all completed: " << theAlert->handle.torrent_file()->files().file_path(0) )
                        //                        }
                        //                    }
                        break;
                    }
                        
                    case lt::file_completed_alert::alert_type: {
                        auto *theAlert= dynamic_cast<lt::file_completed_alert *>(alert);
                        _LOG( "*** file_completed_alert:" << theAlert->handle.torrent_file()->files().file_path(0) );
                        break;
                    }
                        
                        
                    case lt::torrent_error_alert::alert_type: {
                        auto *theAlert = dynamic_cast<lt::torrent_error_alert *>(alert);
                        //(???+++) when client destructing?
                        _LOG(  m_addressAndPort << ": ERROR!!!: torrent error: " << theAlert->message())
                        break;
                        auto userdata = theAlert->handle.userdata().get<LtClientData>();
                        SIRIUS_ASSERT( userdata != nullptr )
                        if ( userdata != nullptr && userdata->m_dnContexts.size()>0 )
                        {
                            userdata->m_dnContexts.front().m_downloadNotification(   download_status::code::dn_failed,
                                                                                  userdata->m_dnContexts.front().m_infoHash,
                                                                                  userdata->m_dnContexts.front().m_saveAs,
                                                                                  userdata->m_uploadedDataSize,
                                                                                  0,
                                                                                  theAlert->message() );
                        }
                        break;
                    }
                        
#ifdef __APPLE__
#pragma mark --torrent_finished_alert
#endif
                    case lt::torrent_finished_alert::alert_type: {
                        //sleep(1);
                        auto *theAlert = dynamic_cast<lt::torrent_finished_alert*>(alert);
                        //_LOG( "*** finished theAlert->handle.id()=" << theAlert->handle.id() );
                        //dbgPrintActiveTorrents();
                        
                        //auto handle_id = theAlert->handle.id();
                        
                        //                    if ( ltDataToHash(theAlert->handle.info_hashes().v2.data()) == stringToHash("e71463c9ad6ab4205523fc5fe71c82ff4b78088c97735418c3ba8caaaf900d59") )
                        //                    {
                        //                        dbgPrintActiveTorrents();
                        //                    }
                        
                        auto userdata = theAlert->handle.userdata().get<LtClientData>();
                        SIRIUS_ASSERT( userdata != nullptr )
                        
                        SIRIUS_ASSERT( userdata->m_finishStatus ==
                                      LtClientData::FinishStatus::TORRENT_ADDED );
                        
                        userdata->m_finishStatus = LtClientData::FinishStatus::TORRENT_FINISHED;
                        break;
                    }
                        
#ifdef __APPLE__
#pragma mark --torrent_deleted_alert
#endif
                    case lt::torrent_deleted_alert::alert_type: {
                        auto *theAlert = dynamic_cast<lt::torrent_deleted_alert*>(alert);
                        _LOG( "*** torrent_deleted_alert:" << theAlert->handle.info_hashes().v2 << " " << theAlert->handle.torrent_file()->files().file_name(0) );
                        //_LOG( "*** deleted get_torrents().size()=" << m_session.get_torrents().size() );
                        //dbgPrintActiveTorrents();
                        break;
                    }
                        
                    case lt::request_dropped_alert::alert_type: {
                        LOG("!!!!! request_dropped_alert");
                        break;
                    }
                        
                    case lt::storage_moved_alert::alert_type: {
                        LOG("!!!!! storage_moved_alert");
                        break;
                    }
                        
                        //                case lt::block_finished_alert::alert_type: {
                        //                    LOG("!!!!! block_finished_alert");
                        //                    break;
                        //                }
                        
                    case lt::portmap_error_alert::alert_type: {
                        auto *theAlert = dynamic_cast<lt::portmap_error_alert *>(alert);
                        
                        if ( theAlert ) {
                            LOG(  "portmap error: " << theAlert->message())
                        }
                        break;
                    }
                        
                    case lt::dht_error_alert::alert_type: {
                        auto *theAlert = dynamic_cast<lt::dht_error_alert *>(alert);
                        
                        if ( theAlert ) {
                            LOG(  "dht error: " << theAlert->message())
                        }
                        break;
                    }
                        
                    case lt::session_error_alert::alert_type: {
                        auto *theAlert = dynamic_cast<lt::session_error_alert *>(alert);
                        
                        if ( theAlert ) {
                            LOG(  "session error: " << theAlert->message())
                        }
                        break;
                    }
                        
                    case lt::udp_error_alert::alert_type: {
                        auto *theAlert = dynamic_cast<lt::udp_error_alert *>(alert);
                        
                        if ( theAlert ) {
                            LOG(  "udp error: " << theAlert->message())
                        }
                        break;
                    }
                        
                    case lt::peer_error_alert::alert_type: {
                        auto *theAlert = dynamic_cast<lt::peer_error_alert *>(alert);
                        
                        if ( theAlert ) {
                            LOG(  m_addressAndPort << ": peer error: " << theAlert->message())
                        }
                        break;
                    }
                        
                    case lt::file_error_alert::alert_type: {
                        auto *theAlert = dynamic_cast<lt::file_error_alert *>(alert);
                        
                        if ( theAlert ) {
                            LOG(  "file error: " << theAlert->message())
                        }
                        break;
                    }
                        
                    case lt::block_uploaded_alert::alert_type: {
                        //                    auto *theAlert = dynamic_cast<lt::block_uploaded_alert *>(alert);
                        //                    //TODO download statistic!
                        //                    if (theAlert) {
                        //                        LOG("block_uploaded: " << theAlert->message())
                        //
                        //                        // get peers info
                        //                        std::vector<lt::peer_info> peers;
                        //                        theAlert->handle.get_peer_info(peers);
                        //
                        //                        for (const lt::peer_info &pi : peers) {
                        //                            LOG("Upload. client: " << pi.client);
                        //                            LOG("Upload. ip: " << pi.ip);
                        //                            LOG("Upload. pid: " << pi.pid.to_string());
                        //                            LOG("Upload. local_endpoint: " << pi.local_endpoint);
                        //
                        //                             //the total number of bytes downloaded from and uploaded to this peer.
                        //                             //These numbers do not include the protocol chatter, but only the
                        //                             //payload data.
                        //                            LOG("Upload. Total download: " << pi.total_download)
                        //                            LOG("Upload. Total upload: " << pi.total_upload)
                        //                        }
                        //                    }
                        break;
                    }
                        
                        //                case lt::portmap_log_alert::alert_type: {
                        //                    auto *theAlert = dynamic_cast<lt::portmap_log_alert *>(alert);
                        //                    _LOG( "portmap_log_alert: " << theAlert->log_message() )
                        //                    break;
                        //                }
                        
#ifdef __APPLE__
#pragma mark --dht_pkt_alert
#endif
//                    case lt::dht_pkt_alert::alert_type: {
//                        auto *theAlert = dynamic_cast<lt::dht_pkt_alert *>(alert);
//                        ___LOG( m_listeningPort << " : dht_pkt_alert: " << theAlert->message() ) //<< " " << theAlert->outgoing )
//                        break;
//                    }
                        
                    default: {
                        //                    if ( alert->type() != 52 && alert->type() != 78 && alert->type() != 86
                        //                        && alert->type() != 81 && alert->type() != 85
                        //                        && m_addressAndPort == "192.168.1.101:5551") //52==portmap_log_alert
                        //                    {
                        //                        LOG( m_addressAndPort << " alert(" << alert->type() << "): " << alert->message() );
                        //                    }
                    }
                }
            }
        }
        catch(std::runtime_error& ex)
        {
            _LOG( "!!!ERROR!!!: unhandled exception: " << ex.what() );
            _LOG( "!!!ERROR!!!: unhandled exception in alertHandler()");
        }
        catch(...)
        {
            _LOG( "!!!ERROR!!!: unhandled exception in alertHandler()");
        }
        
    }

#ifdef __APPLE__
#pragma mark --KademliaTransport--
#endif

    virtual void sendGetMyIpRequest( const kademlia::MyIpRequest& request, boost::asio::ip::udp::endpoint endpoint ) override
    {
        std::ostringstream os( std::ios::binary );
        cereal::PortableBinaryOutputArchive archive( os );
        archive( request );

        sendMessage( GET_MY_IP_MSG, endpoint, os.str() );
    }

    virtual void sendGetPeerIpRequest( const kademlia::PeerIpRequest& request, boost::asio::ip::udp::endpoint endpoint ) override
    {
        std::ostringstream os( std::ios::binary );
        cereal::PortableBinaryOutputArchive archive( os );
        archive( request );

        sendMessage( GET_PEER_IP_MSG, endpoint, os.str() );
    }

    virtual std::string onGetMyIpRequest( const std::string& request, boost::asio::ip::udp::endpoint requesterEndpoint ) override
    {
        return m_kademlia->onGetMyIpRequest( request, requesterEndpoint );
    }

    virtual std::string onGetPeerIpRequest( const std::string& request, boost::asio::ip::udp::endpoint requesterEndpoint ) override
    {
        return m_kademlia->onGetPeerIpRequest( request, requesterEndpoint );
    }

    virtual void onGetMyIpResponse( const std::string& response, boost::asio::ip::udp::endpoint responserEndpoint ) override
    {
        m_kademlia->onGetMyIpResponse( response, responserEndpoint );
    }

    virtual void onGetPeerIpResponse( const std::string&  response, boost::asio::ip::udp::endpoint responserEndpoint ) override
    {
        m_kademlia->onGetPeerIpResponse( response, responserEndpoint );
    }
};

//
// ('static') createTorrentFile
//
InfoHash createTorrentFile( const fs::path& fileOrFolder,
                            const Key&      drivePublicKey,
                            const fs::path& rootFolder,
                            const fs::path& outputTorrentFilename )
{
    // setup file storage
    lt::file_storage fStorage;
    lt::add_files( fStorage, fileOrFolder.string().c_str(), lt::create_flags_t{} );

    // create torrent info
    lt::create_torrent createInfo( fStorage, PIECE_SIZE, lt::create_torrent::v2_only );

    // calculate hashes for 'fileOrFolder' relative to 'rootFolder'
    lt::error_code ec;
    lt::set_piece_hashes( createInfo, rootFolder.string().c_str(), ec );
    if ( ec ) {
        __LOG_WARN( "createTorrentFile error: " << ec.message() << " code: " << ec.value() )
        return {};
    }

    // generate metadata
    lt::entry entry_info = createInfo.generate();

    // add public key of drive
    entry_info["info"].dict()["sirius drive"]=lt::entry( toString(drivePublicKey.array()) );

    // convert to bencoding
    std::vector<char> torrentFileBytes;
    lt::bencode(std::back_inserter(torrentFileBytes), entry_info); // metainfo -> binary

    //dbg////////////////////////////////
    auto entry = entry_info;
    //LOG( "entry[info]:" << entry["info"].to_string() );
    __LOG( entry.to_string() );
    auto tInfo = lt::torrent_info(torrentFileBytes, lt::from_span);
    //LOG( "make_magnet_uri:" << lt::make_magnet_uri(tInfo) );
    //dbg////////////////////////////////

    // get infoHash
    lt::torrent_info torrentInfo( torrentFileBytes, lt::from_span );
    auto binaryString = torrentInfo.info_hashes().v2.to_string();

    // copy hash
    InfoHash infoHash;
    if ( binaryString.size()==32 ) {
        memcpy( &infoHash[0], &binaryString[0], 32 );
    }

    // write to file
    if ( !outputTorrentFilename.empty() )
    {
        __LOG( "outputTorrentFilename: " << outputTorrentFilename )
        std::ofstream fileStream( outputTorrentFilename, std::ios::binary );
        fileStream.write(torrentFileBytes.data(),(std::streamsize)torrentFileBytes.size());
    }

    return infoHash;
}

InfoHash calculateInfoHashAndCreateTorrentFile( const std::filesystem::path& pathToFile,
                                                const Key&                   drivePublicKey,
                                                const std::filesystem::path& outputTorrentPath,
                                                const std::filesystem::path& outputTorrentFileExtension )
{
    if ( fs::is_directory(pathToFile) )
    {
        throw std::runtime_error( std::string("moveFileToFlatDrive: 1-st parameter cannot be a folder: ") + pathToFile.string() );
    }

    if ( !outputTorrentPath.empty() && !fs::is_directory(outputTorrentPath) )
    {
        throw std::runtime_error( std::string("moveFileToFlatDrive: 'outputTorrentPath' must be a folder: ") + pathToFile.string() );
    }

    // setup file storage
    lt::file_storage fStorage;
    lt::add_files( fStorage, pathToFile.string().c_str(), lt::create_flags_t{} );

    // create torrent info
    lt::create_torrent createInfo( fStorage, PIECE_SIZE, lt::create_torrent::v2_only );

    // calculate hashes
    lt::error_code ec;
    lt::set_piece_hashes( createInfo, pathToFile.parent_path().string().c_str(), ec );
    if ( ec )
    {
        __LOG_WARN( "calculateInfoHashAndCreateTorrentFile error: " << ec.message() << " code: " << ec.value() );
        return {};
    }

    // generate metadata tree
    lt::entry entry_info = createInfo.generate();

    // add public key of drive
    entry_info["info"].dict()["sirius drive"]=lt::entry( toString(drivePublicKey.array()) );

    // convert to bencoding
    std::vector<char> torrentFileBytes;
    lt::bencode(std::back_inserter(torrentFileBytes), entry_info); // metainfo -> binary

    //dbg////////////////////////////////
    //LOG( "entry_info[info]:" << entry_info["info"].to_string() );
    //dbg////////////////////////////////

    // create torrentInfo
    lt::torrent_info torrentInfo( torrentFileBytes, lt::from_span );
    auto binaryString = torrentInfo.info_hashes().v2.to_string();

    // copy hash
    InfoHash infoHash;
    if ( binaryString.size()==32 ) {
        memcpy( infoHash.data(), binaryString.data(), 32 );
    }
    std::string newFileName = hashToFileName(infoHash);

    // replace file name
    auto& info = entry_info["info"];
    auto& fileTreeDict = info["file tree"].dict();
    auto fileInfo = fileTreeDict.begin()->second;
    fileTreeDict.erase( fileTreeDict.begin() );
    fileTreeDict[newFileName] = fileInfo;
    auto name = info.dict().find("name");
    info.dict().erase( name );
    info.dict()["name"] = newFileName;

    //LOG( "entry[info]:" << entry_info["info"].to_string() );

    std::vector<char> torrentFileBytes2;
    lt::bencode(std::back_inserter(torrentFileBytes2), entry_info); // metainfo -> binary
    lt::torrent_info torrentInfo2( torrentFileBytes2, lt::from_span );

    // get infoHash
    auto binaryString2 = torrentInfo2.info_hashes().v2.to_string();

    // copy hash
    InfoHash infoHash2;
    if ( binaryString2.size()==32 ) {
        memcpy( infoHash2.data(), binaryString2.data(), 32 );
    }

    LOG( "file infoHash :" << toString(infoHash) );
    LOG( "infoHash2 :" << toString(infoHash2) );
    LOG( "pathToFile :" << pathToFile );
    LOG( "drivePublicKey :" << drivePublicKey );
    assert( infoHash == infoHash2 );

    // write to file
    if ( !outputTorrentPath.empty() )
    {
        std::ofstream fileStream( fs::path(outputTorrentPath.string() + "/" + newFileName + outputTorrentFileExtension.string()).make_preferred(), std::ios::binary );
        fileStream.write( torrentFileBytes2.data(), torrentFileBytes2.size() );
    }

    return infoHash;
}

InfoHash calculateInfoHash( const std::filesystem::path& pathToFile, const Key& drivePublicKey )
{
    if ( fs::is_directory(pathToFile) )
    {
        throw std::runtime_error( std::string("calculateInfoHash: 1-st parameter cannot be a folder: ") + pathToFile.string() );
    }

    // setup file storage
    lt::file_storage fStorage;
    lt::add_files( fStorage, pathToFile.string(), lt::create_flags_t{} );

    // create torrent info
    lt::create_torrent createInfo( fStorage, PIECE_SIZE, lt::create_torrent::v2_only );

    // calculate hashes
    lt::error_code ec;
    lt::set_piece_hashes( createInfo, pathToFile.parent_path().string(), ec);
    if ( ec )
    {
        __LOG_WARN( "calculateInfoHash error: " << ec.message() << " code: " << ec.value() );
        return {};
    }

    // generate metadata tree
    lt::entry entry_info = createInfo.generate();

    // add public key of drive
    entry_info["info"].dict()["sirius drive"]=lt::entry( toString(drivePublicKey.array()) );

    // convert to bencoding
    std::vector<char> torrentFileBytes;
    lt::bencode(std::back_inserter(torrentFileBytes), entry_info); // metainfo -> binary

    //dbg////////////////////////////////
    //LOG( "entry_info[info]:" << entry_info["info"].to_string() );
    //dbg////////////////////////////////

    // create torrentInfo
    lt::torrent_info torrentInfo( torrentFileBytes, lt::from_span );
    auto binaryString = torrentInfo.info_hashes().v2.to_string();

    // copy hash
    InfoHash infoHash;
    if ( binaryString.size()==32 ) {
        memcpy( infoHash.data(), binaryString.data(), 32 );
    }

    return infoHash;
}

//
// For Replicator
//

std::shared_ptr<Session> createDefaultSession( boost::asio::io_context&             context,
                                               std::string                          address,
                                               const LibTorrentErrorHandler&        alertHandler,
                                               std::weak_ptr<ReplicatorInt>         replicator,
                                               std::weak_ptr<lt::session_delegate>  downloadLimiter,
                                              const std::vector<ReplicatorInfo>&    bootstraps,
                                               std::promise<void>&&                 bootstrapBarrier )
{
    return std::make_shared<DefaultSession>( context,
                                            address,
                                            alertHandler,
                                            replicator,
                                            downloadLimiter,
                                            bootstraps,
                                            std::move(bootstrapBarrier) );
}

//
// For Client
//
std::shared_ptr<Session> createDefaultSession( std::string                          address,
                                               const crypto::KeyPair&               keyPair,
                                               const LibTorrentErrorHandler&        alertHandler,
                                               std::weak_ptr<lt::session_delegate>  downloadLimiter,
                                               const std::vector<ReplicatorInfo>&   bootstraps,
                                               std::weak_ptr<DhtMessageHandler>     dhtMessageHandler )
{
    return std::make_shared<DefaultSession>( address, keyPair, alertHandler, downloadLimiter, false, bootstraps, dhtMessageHandler );
}

}

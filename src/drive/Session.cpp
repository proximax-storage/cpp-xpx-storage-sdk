/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "drive/Session.h"
#include "ReplicatorInt.h"
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

enum { PIECE_SIZE = 16*1024 };

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
    
    std::shared_ptr<RemoveNotifyer> m_removeNotifyer;

    fs::path                        m_saveFolder          = {};
    fs::path                        m_saveTorrentFilename = {};
    std::vector<DownloadContext>    m_dnContexts          = {};

    uint64_t                        m_downloadLimit       = 0;
    uint64_t                        m_uploadedDataSize    = 0;
    bool                            m_limitIsExceeded     = false;
    
    bool                            m_isRemoved           = false;
};

//
// DefaultSession
//
class DefaultSession: public Session, std::enable_shared_from_this<DefaultSession>
{
    bool                    m_ownerIsReplicator = true;
    
    std::string             m_addressAndPort;
    lt::session             m_session;

    // It will be called on socket listening error
    LibTorrentErrorHandler  m_alertHandler;

    std::weak_ptr<ReplicatorInt>        m_replicator;
    std::weak_ptr<lt::session_delegate> m_downloadLimiter;
    
    std::promise<void>                  m_bootstrapBarrier;
    
    std::string                         m_dbgOurPeerName = "";
    
    bool                                m_stopping = false;
    
    std::thread::id                     m_dbgThreadId;


public:
    
    // Constructor for Replicator
    //
    DefaultSession( boost::asio::io_context&             context,
                    std::string                          address,
                    const LibTorrentErrorHandler&        alertHandler,
                    std::weak_ptr<ReplicatorInt>         replicator,
                    std::weak_ptr<lt::session_delegate>  downloadLimiter,
                    const endpoint_list&                 bootstraps,
                    std::promise<void>&&                 bootstrapBarrier
                    )
        : m_ownerIsReplicator(true)
        , m_addressAndPort(address)
        , m_session( lt::session_params{ generateSessionSettings( false, bootstraps) }, context, {})
        , m_alertHandler(alertHandler)
        , m_replicator(replicator)
        , m_downloadLimiter(downloadLimiter)
        , m_bootstrapBarrier( std::move(bootstrapBarrier) )
    {
        m_dbgOurPeerName = m_downloadLimiter.lock()->dbgOurPeerName();
        
        continueSessionCreation();
        m_session.setDelegate( m_downloadLimiter );
        _LOG( "DefaultSession created: " );
        _LOG( "DefaultSession created: " << m_addressAndPort );
        _LOG( "DefaultSession created: " << m_addressAndPort << " " << m_replicator.lock() );
        _LOG( "DefaultSession created: " << m_addressAndPort << " " << int(m_replicator.lock()->replicatorKey()[0]) );
    }

    // Constructor for Client
    //
    DefaultSession( std::string                             address,
                    LibTorrentErrorHandler                  alertHandler,
                    std::weak_ptr<lt::session_delegate>     downloadLimiter,
                    bool                                    useTcpSocket,
                    const endpoint_list&                    bootstraps
                    )
        : m_ownerIsReplicator(false)
        , m_addressAndPort(address)
        , m_session( lt::session_params{ generateSessionSettings( useTcpSocket, bootstraps ) } )
        , m_alertHandler(alertHandler)
        , m_downloadLimiter(downloadLimiter)
        , m_bootstrapBarrier()
    {
        if ( downloadLimiter.lock() )
            m_dbgOurPeerName = downloadLimiter.lock()->dbgOurPeerName();
        
        continueSessionCreation();
        m_session.setDelegate( m_downloadLimiter );
        _LOG( "DefaultSession created: " << m_addressAndPort );
    }

    virtual ~DefaultSession()
    {
        //m_session.stop_dht();
        // lt::settings_pack p;
        // p.set_bool(lt::settings_pack::enable_dht, false);
        // m_session.apply_settings(p);
        //m_session.pause();

        m_stopping = true;
    }

    // for dbg
    lt::session &lt_session() override
    {
        return m_session;
    }
    
    virtual void onTorrentDeleted( lt::torrent_handle handle ) override
    {
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

        if ( ! contextVector.empty() && ! userdata->m_limitIsExceeded )
        {
            fs::path srcFilePath = fs::path(userdata->m_saveFolder) / hashToFileName( contextVector[0].m_infoHash );

            for( size_t i=0; i<contextVector.size(); i++ )
            {
                if ( contextVector[i].m_doNotDeleteTorrent )
                {
                    continue;
                }
                
                auto& context = contextVector[i];

                if ( ! context.m_saveAs.empty() && context.m_downloadType == DownloadContext::file_from_drive )
                {
                    _ASSERT( ! m_ownerIsReplicator )
                    
                    fs::path destFilePath = context.m_saveAs;

                    std::error_code err;
                    if ( !fs::exists( destFilePath.parent_path(), err ) ) {
                        fs::create_directories( destFilePath.parent_path(), err );
                    }

                    //???+++
                    if ( fs::exists( destFilePath, err ) ) {
                        fs::remove( destFilePath );
                    }

                    if ( i == contextVector.size()-1 )
                    {
                        fs::rename( srcFilePath, destFilePath, err );
                    }
                    else
                    {
                        fs::copy( srcFilePath, destFilePath, err );
                    }
                }

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


    lt::settings_pack generateSessionSettings(bool useTcpSocket, const endpoint_list& bootstraps)
    {
        lt::settings_pack settingsPack;

        settingsPack.set_int( lt::settings_pack::alert_mask, ~0 );//lt::alert_category::all );

        // todo public_key?
        char todoPubKey[32];
        std::memset(todoPubKey,'x', sizeof(todoPubKey));
        todoPubKey[5] = 0;
        settingsPack.set_str(  lt::settings_pack::user_agent, std::string(todoPubKey,32) );

        if ( useTcpSocket )
        {
            settingsPack.set_bool( lt::settings_pack::enable_outgoing_utp, false );
            settingsPack.set_bool( lt::settings_pack::enable_incoming_utp, false );
            settingsPack.set_bool( lt::settings_pack::enable_outgoing_tcp, true );
            settingsPack.set_bool( lt::settings_pack::enable_incoming_tcp, true );
        }

        //todo 1. is it enough? 2. is it for single peer?
        settingsPack.set_int( lt::settings_pack::dht_upload_rate_limit, 8000000 );

        settingsPack.set_bool( lt::settings_pack::enable_dht, true );
        settingsPack.set_bool( lt::settings_pack::enable_lsd, false ); // is it needed?
        settingsPack.set_bool( lt::settings_pack::enable_upnp, true );

        std::ostringstream bootstrapsBuilder;
        for ( const auto& bootstrap: bootstraps )
        {
            bootstrapsBuilder << bootstrap.address().to_string() << ":" << std::to_string(bootstrap.port()) << ",";
        }

        std::string bootstrapList = bootstrapsBuilder.str();

        settingsPack.set_str(  lt::settings_pack::dht_bootstrap_nodes, bootstrapList);

        settingsPack.set_str(  lt::settings_pack::listen_interfaces, m_addressAndPort );
        settingsPack.set_bool( lt::settings_pack::allow_multiple_connections_per_ip, false );
        settingsPack.set_bool( lt::settings_pack::enable_ip_notifier, false );
        
        return settingsPack;
    }

    void setDbgThreadId()
    {
        m_dbgThreadId = std::this_thread::get_id();
    }
    
    // createSession
    void continueSessionCreation() {
        m_session.set_alert_notify( [this] { alertHandler(); } );
        addDhtRequestPlugin();
    }

    virtual void endSession() override {
        m_stopping = true;
        _LOG( "stop session" )
    }
    
    virtual bool isEnding() override {
        return m_stopping;
    }


    virtual void removeTorrentsFromSession( const std::set<lt::torrent_handle>&  torrents,
                                            std::function<void()>                endNotification ) override
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
                    __ASSERT( torrentHandle.userdata().get<LtClientData>() != nullptr )
                    __ASSERT( ! torrentHandle.userdata().get<LtClientData>()->m_removeNotifyer )
                    torrentHandle.userdata().get<LtClientData>()->m_removeNotifyer = removeNotifyer;
                }
            }

            // races!
            removeNotifyer.reset();
            
            for( const auto& torrentHandle : torrents )
            {
//                if ( torrentHandle.userdata().get<LtClientData>()->m_removeNotifyer )
//                {
                    _LOG( "+++ ex :remove_torrent(3): " << torrentHandle.info_hashes().v2 );
                    m_session.remove_torrent( torrentHandle, lt::session::delete_files );
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
    
    virtual lt_handle addTorrentFileToSession( const std::string&               torrentFilename,
                                               const std::string&               folderWhereFileIsLocated,
                                               lt::SiriusFlags::type            siriusFlags,
                                               const std::array<uint8_t,32>*    driveKey,
                                               const std::array<uint8_t,32>*    channelId,
                                               const std::array<uint8_t,32>*    modifyTx,
                                               endpoint_list list,
                                               uint64_t* outTotalSize ) override
    {
        // read torrent file
        std::ifstream torrentFile( torrentFilename );
        std::vector<char> buffer( (std::istreambuf_iterator<char>(torrentFile)), std::istreambuf_iterator<char>() );

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
        params.save_path        = fs::path(folderWhereFileIsLocated);
        params.ti               = std::make_shared<lt::torrent_info>( buffer, lt::from_span );
        params.m_siriusFlags    = siriusFlags;
        if ( driveKey )
            params.m_driveKey = *driveKey;
        if ( channelId )
            params.m_channelId = *channelId;
        if ( modifyTx )
            params.m_modifyTx = *modifyTx;

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
        }

        return tHandle;
    }

    // downloadFile
    virtual lt_handle download( DownloadContext&&               downloadContext,
                                const std::string&              saveFolder,
                                const std::string&              saveTorrentFolder,
                                const ReplicatorList&           keysHints,
                                const std::array<uint8_t,32>*   driveKey  = nullptr,
                                const std::array<uint8_t,32>*   channelId = nullptr,
                                const std::array<uint8_t,32>*   modifyTx  = nullptr,
                                const endpoint_list&            endpointsHints = {} ) override
    {
        // create add_torrent_params
        lt::error_code ec;
        lt::add_torrent_params params = lt::parse_magnet_uri( magnetLink(downloadContext.m_infoHash), ec );
        
        if (ec) {
            throw std::runtime_error( std::string("downloadFile error: ") + ec.message() );
        }

        auto userdata = new LtClientData();
        userdata->m_saveTorrentFilename = saveTorrentFolder;
        params.userdata = userdata;

        // where the file will be placed
        params.save_path = saveFolder;

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
                //LOG( "connect_peer: " << endpoint.address() << ":" << endpoint.port() );
                auto endpoint = limiter->getEndpoint( key.array() );
                if ( endpoint )
                {
                    tHandle.connect_peer( *endpoint );
                }
            }
        }

        for ( const auto& endpoint: endpointsHints )
        {
            tHandle.connect_peer( endpoint );
        }

        // set fs tree save path
        if ( downloadContext.m_downloadType == DownloadContext::fs_tree ) {
            downloadContext.m_saveAs = fs::path(saveFolder) / FS_TREE_FILE_NAME;
        }
        else if ( downloadContext.m_downloadType == DownloadContext::file_from_drive ) {
            if (downloadContext.m_saveAs.empty())
                throw std::runtime_error("download(file_from_drive): DownloadContext::m_saveAs' is empty");
        }

        __ASSERT( tHandle.userdata().get<LtClientData>() != nullptr )
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
            tHandle.connect_peer(endpoint);
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
//        std::weak_ptr<lt::session_delegate> m_replicator;
//        DhtRequestPlugin( std::weak_ptr<lt::session_delegate> replicator ) : m_replicator(replicator) {}
        std::weak_ptr<ReplicatorInt> m_replicator;
        DhtRequestPlugin( std::weak_ptr<ReplicatorInt> replicator ) : m_replicator(replicator) {}

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
            //__LOG( "on_dht_request: query: " << query );
            
            if ( auto replicator = m_replicator.lock(); ! replicator || replicator->isStopped() )
            {
                return false;
            }

            if ( query == "get_peers" || query == "announce_peer" )
            {
                //(???) NULL dht_direct_response_alert?
                return false;
            }

//            _LOG( "message: " << message );
//            _LOG( "response: " << response );

            const std::set<lt::string_view> supportedQueries =
                    { "opinion", "dn_opinion", "code_verify", "verify_opinion", "handshake", "endpoint_request", "endpoint_response",
                       "chunk-info"
                    };
            if ( supportedQueries.contains(query) )
            {
                auto str = message.dict_find_string_value("x");
                std::string packet( (char*)str.data(), (char*)str.data()+str.size() );

                if ( auto replicator = m_replicator.lock(); replicator )
                {
                    replicator->onMessageReceived( std::string(query.begin(),query.end()), packet, source );
                }

                response["r"]["q"] = std::string(query);
                response["r"]["ret"] = "ok";
                return true;
            }
            
            else if ( query == "get_dn_rcpts" )
            {
                // extract signature
                auto sign = message.dict_find_string_value("sign");
                Signature signature;
                if ( sign.size() != signature.size() )
                {
                    __LOG_WARN( "invalid query 'get_dn_rcpts'" )
                    return true;
                }
                memcpy( signature.data(), sign.data(), signature.size() );

                // extract message
                auto str = message.dict_find_string_value("x");

                // extract request fields
                //
                ReplicatorKey senderKey;
                DriveKey      driveKey;
                ChannelId     channelId;

                uint8_t* ptr = (uint8_t*)str.data();
                memcpy( senderKey.data(), ptr, senderKey.size() );
                ptr += senderKey.size();
                memcpy( driveKey.data(), ptr, driveKey.size() );
                ptr += driveKey.size();
                memcpy( channelId.data(), ptr, channelId.size() );

                if ( ! crypto::Verify( senderKey, { utils::RawBuffer{(const uint8_t*) str.data(), str.size()} }, signature ) )
                {
                    __LOG_WARN( "invalid signature of 'get_dn_rcpts'" )
                    return true;
                }


                if ( auto replicator = m_replicator.lock(); replicator )
                {
                    std::ostringstream os( std::ios::binary );
                    Signature responseSignature;
                    if ( replicator->createSyncRcpts( driveKey, channelId, os, responseSignature ) )
                    {
                        __LOG( "response[r][q]: " << query );
                        response["r"]["q"] = std::string(query);
                        response["r"]["ret"] = os.str();
                        response["r"]["sign"] = std::string( responseSignature.begin(), responseSignature.end() );
                    }
                    return true;
                }
            }
                
            else if ( query == "rcpt" )
            {
                auto str = message.dict_find_string_value("x");

                RcptMessage msg( str.data(), str.size() );
                
                if ( ! msg.isValidSize() )
                {
                    __LOG( "WARNING!!!: invalid rcpt size" )
                    return false;
                }
                
                if ( auto replicator = m_replicator.lock(); replicator )
                {
                    replicator->acceptReceiptFromAnotherReplicator( msg );
                }

                response["r"]["q"] = std::string(query);
                response["r"]["ret"] = "ok";
                return true;
            }
            
            return false;
        }
    };

    void addDhtRequestPlugin()
    {
        m_session.add_extension(std::make_shared<DhtRequestPlugin>(  m_replicator ));
    }

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
        lt::entry entry;
        entry["q"] = query;
        entry["x"] = message;

        m_session.dht_direct_request( endPoint, entry );
    }

//    void requestDownloadReceipts( boost::asio::ip::udp::endpoint endPoint,
//                                  const std::array<uint8_t,32>& driveKey,
//                                  const std::array<uint8_t,32>& downloadChannelHash )// override
//    {
//        std::vector<uint8_t> message;
//        message.insert( message.end(), driveKey.begin(), driveKey.end() );
//        message.insert( message.end(), downloadChannelHash.begin(), downloadChannelHash.end() );
//
//        lt::entry entry;
//        entry["q"] = "get_dn_repts";
//        entry["x"] = std::string( message.begin(), message.end() );
//
//        m_session.dht_direct_request( endPoint, entry );
//    }



    void findAddress( const Key& key ) override
    {
        auto publicKey = *reinterpret_cast<const std::array<char, 32> *>(key.data());
        m_session.dht_get_item( publicKey, "ip" );
    }

    void announceExternalAddress( const boost::asio::ip::tcp::endpoint& endpoint ) override
    {
        if ( auto limiter = m_downloadLimiter.lock(); limiter )
        {
            std::string data(reinterpret_cast<const char *>(&endpoint), sizeof(boost::asio::ip::tcp::endpoint));
            auto publicKey = *reinterpret_cast<const std::array<char, 32> *>(limiter->publicKey().data());
            m_session.dht_put_item(publicKey,
                                   [limiter = m_downloadLimiter, d = std::move(data)]
                                           (lt::entry &e, std::array<char, 64> &sig, std::int64_t &seq,
                                            std::string const &salt)
                                   {
                                       if ( auto p = limiter.lock(); p )
                                       {
                                           e = d;
                                           std::vector<char> buf;
                                           bencode(std::back_inserter(buf), e);
                                           seq++;
                                           std::array<uint8_t, 64> signature{};
                                           p->signMutableItem(buf, seq, salt, signature);
                                           std::copy(signature.begin(), signature.end(), sig.begin());
                                       }
                                   },
                                   "ip");
        }
    }

    void handleDhtResponse( lt::bdecode_node response )
    {
        try
        {
            auto rDict = response.dict_find_dict("r");
            auto query = rDict.dict_find_string_value("q");
            if ( query == "get_dn_rcpts" )
            {
                lt::string_view response = rDict.dict_find_string_value("ret");
                lt::string_view sign = rDict.dict_find_string_value("sign");
                m_replicator.lock()->onSyncRcptReceived( response, sign );
            }
        }
        catch(...)
        {
        }
    }
    
    virtual std::optional<boost::asio::high_resolution_timer> startTimer( int miliseconds, const std::function<void()>& func ) override
    {
        auto delegate = m_downloadLimiter.lock();
        if ( !delegate || delegate->isStopped() )
        {
            return {};
        }

        boost::asio::high_resolution_timer timer( m_session.get_context() );
        
        timer.expires_after( std::chrono::milliseconds( miliseconds ) );
        timer.async_wait( [func=func] (boost::system::error_code const& e) {
            if ( !e )
            {
                func();
            }
        });
        
        return timer;
    }


private:

    void processEndpointItem(lt::dht_mutable_item_alert* theAlert)
    {
        if ( theAlert->seq < 0 )
        {
            return;
        }
        auto limiter = m_downloadLimiter.lock();
        if ( !limiter )
        {
            return;
        }
        auto publicKey = *reinterpret_cast<std::array<uint8_t, 32> *>( &theAlert->key );
        if ( theAlert->seq == 0 )
        {
            limiter->onEndpointDiscovered(publicKey, {});
            return;
        }
        auto response = theAlert->item.string();
        if ( response.size() != sizeof(boost::asio::ip::tcp::endpoint) )
        {
            return;
        }
        auto endpoint = *reinterpret_cast<boost::asio::ip::tcp::endpoint*>(response.data());
        limiter->onEndpointDiscovered(publicKey, endpoint);
    }

    void alertHandler()
    {
        //DBG_MAIN_THREAD
        
        if ( m_stopping )
        {
            return;
        }

        // extract alerts
        std::vector<lt::alert *> alerts;
        m_session.pop_alerts(&alerts);

        // loop by alerts
        for (auto &alert : alerts) {

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
            switch (alert->type()) {
                    
                //todo++++
//                case lt::torrent_log_alert::alert_type:
//                {
//                    auto* theAlert = dynamic_cast<lt::torrent_log_alert*>(alert);
//                    _LOG( "torrent_log_alert:" << m_addressAndPort << ": " <<  theAlert->message() );
//                    break;
//                }

//                case lt::peer_log_alert::alert_type: {
//                    _LOG(  ": peer_log_alert: " << alert->message())
//                    break;
//                }

//                case lt::log_alert::alert_type: {
//                    _LOG(  ": session_log_alert: " << alert->message())
//                    break;
//                }

                case lt::dht_bootstrap_alert::alert_type: {
                    _LOG( "dht_bootstrap_alert: " << alert->message() )
                    //m_bootstrapBarrier.set_value();
                    break;
                }

                case lt::external_ip_alert::alert_type: {
                    auto* theAlert = dynamic_cast<lt::external_ip_alert*>(alert);
                    _LOG( "External Ip Alert " << " " << theAlert->message())
                    break;
                }

                case lt::dht_announce_alert::alert_type: {
                    break;
                }

                case lt::dht_immutable_item_alert::alert_type: {
                    break;
                }

                case lt::dht_mutable_item_alert::alert_type: {

                    auto* theAlert = dynamic_cast<lt::dht_mutable_item_alert*>(alert);
                    if ( theAlert->salt == "ip" )
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
                        
                        if ( userdata != nullptr )
                        {
                            int64_t downloadLimit = (userdata->m_dnContexts.size() == 0) ? 0 : userdata->m_dnContexts.front().m_downloadLimit;
                            userdata->m_uploadedDataSize = theAlert->handle.torrent_file()->total_size();

                            bool limitIsExceeded = downloadLimit != 0 && downloadLimit < theAlert->handle.torrent_file()->total_size();
                            _LOG( "+**** limitIsExceeded?: " << downloadLimit << " " << theAlert->handle.torrent_file()->total_size() );
                            if ( limitIsExceeded )
                            {
                                _LOG( "+**** limitIsExceeded: " << theAlert->handle.torrent_file()->total_size() );
                                m_session.remove_torrent( theAlert->handle, lt::session::delete_files );

                                userdata->m_limitIsExceeded = true;
                                _ASSERT( userdata->m_dnContexts.size()==1 )
                                userdata->m_dnContexts.front().m_downloadNotification(
                                                                    download_status::code::failed,
                                                                    userdata->m_dnContexts.front().m_infoHash,
                                                                    userdata->m_dnContexts.front().m_saveAs,
                                                                    userdata->m_uploadedDataSize,
                                                                    0,
                                                                    "Limit Is Exceeded" );
                            }
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
                             //_LOG( "dht_query: " << query )
                             if ( query == "get_dn_rcpts" )
                             {
                                 handleDhtResponse( response);
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

                // piece_finished_alert
                case lt::piece_finished_alert::alert_type:
                {
//                    auto *theAlert = dynamic_cast<lt::piece_finished_alert *>(alert);
//                    if ( theAlert ) _LOG( "piece_finished_alert: " << theAlert->handle.torrent_file()->files().file_path(0) );
//
//                    if ( theAlert ) {
//
//                        // TODO: better to use piece_granularity
//                        std::vector<int64_t> fp = theAlert->handle.file_progress();// lt::torrent_handle::piece_granularity );
//
#ifdef __APPLE__
#pragma mark --download-progress--
#endif
//                        //todo++++
//                        bool calculatePercents = true;//false;//true;
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
////                            const std::string filePath = theAlert->handle.torrent_file()->files().file_path(i);
////                            _LOG( m_addressAndPort << ": " << filePath << ": alert: progress: " << fp[i] << " of " << fsize );
//                            //dbg/////////////////////////
//                        }
//
//                        if ( calculatePercents )
//                        {
//                            m_dbgPieceCounter++;
//                            _LOG( m_addressAndPort << ":  progress: " << 100.*double(dnBytes)/double(totalBytes) << "   " << dnBytes << "/" << totalBytes << "  piece_index=" << theAlert->piece_index << "  piece_count=" << m_dbgPieceCounter );
//                        }
//
//                        if ( isAllComplete )
//                        {
//                            _LOG( m_addressAndPort << ": all complete" )
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
                    _ASSERT( userdata != nullptr )
                    if ( userdata != nullptr && userdata->m_dnContexts.size()>0 )
                    {
                        userdata->m_dnContexts.front().m_downloadNotification(   download_status::code::failed,
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
                    _LOG( "*** torrent_finished_alert: " << theAlert->handle.info_hashes().v2 );
                    _LOG( "***                   file: " << theAlert->handle.torrent_file()->files().file_path(0) );
                    _LOG( "***              save_path: " << theAlert->handle.status(lt::torrent_handle::query_save_path).save_path );
                    //_LOG( "*** finished theAlert->handle.id()=" << theAlert->handle.id() );
                    //dbgPrintActiveTorrents();

                    //auto handle_id = theAlert->handle.id();
                    
//                    if ( ltDataToHash(theAlert->handle.info_hashes().v2.data()) == stringToHash("e71463c9ad6ab4205523fc5fe71c82ff4b78088c97735418c3ba8caaaf900d59") )
//                    {
//                        dbgPrintActiveTorrents();
//                    }
                    
                    auto userdata = theAlert->handle.userdata().get<LtClientData>();
                    _ASSERT( userdata != nullptr )
                    
                    if ( userdata->m_isRemoved )
                    {
                        break;
                    }

                    if ( userdata != nullptr && userdata->m_dnContexts.size()>0 )
                    {
                        auto dnContext = userdata->m_dnContexts.front();
                        if ( dnContext.m_doNotDeleteTorrent )
                        {
                            if ( userdata->m_saveTorrentFilename.empty() )
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
                            }
                            else
                            {
                                auto ti = theAlert->handle.torrent_file_with_hashes();

                                // Save torrent file
                                m_replicator.lock()->executeOnBackgroundThread( [ti=std::move(ti),userdata,this]
                                {
                                    lt::create_torrent ct(*ti);
                                    lt::entry te = ct.generate();
                                    std::vector<char> buffer;
                                    bencode(std::back_inserter(buffer), te);

                                    if ( FILE* f = fopen( userdata->m_saveTorrentFilename.c_str(), "wb+" ); f )
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
                                });
                            }
                        }
                        else
                        {
                            _LOG( "***                removed: " << theAlert->handle.info_hashes().v2 );
                            m_session.remove_torrent( theAlert->handle, lt::session::delete_partfile );
                        }
                    }
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




                case lt::listen_failed_alert::alert_type: {
                    this->m_alertHandler( alert );

                    auto *theAlert = dynamic_cast<lt::listen_failed_alert *>(alert);

                    if ( theAlert ) {
                        LOG(  "listen error: " << theAlert->message())
                    }
                    break;
                }

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
};

//
// ('static') createTorrentFile
//
InfoHash createTorrentFile( const std::string& fileOrFolder,
                            const Key&         drivePublicKey,
                            const std::string& rootFolder,
                            const std::string& outputTorrentFilename )
{
    // setup file storage
    lt::file_storage fStorage;
    lt::add_files( fStorage, fileOrFolder, lt::create_flags_t{} );

    // create torrent info
    lt::create_torrent createInfo( fStorage, PIECE_SIZE, lt::create_torrent::v2_only );

    // calculate hashes for 'fileOrFolder' relative to 'rootFolder'
    lt::error_code ec;
    lt::set_piece_hashes( createInfo, rootFolder, ec );
    if ( ec ) {
        __LOG( "createTorrentFile error: " << ec );
        throw std::runtime_error( std::string("createTorrentFile error: ") + ec.message() );
    }
//    std::cout << ec.category().name() << ':' << ec.value();
//    lt::set_piece_hashes( createInfo, fs::path(fileOrFolder).parent_path() );

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
    //LOG( entry.to_string() );
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
        std::ofstream fileStream( outputTorrentFilename, std::ios::binary );
        fileStream.write(torrentFileBytes.data(),torrentFileBytes.size());
    }

    return infoHash;
}

InfoHash calculateInfoHashAndCreateTorrentFile( const std::string& pathToFile,
                                                const Key&         drivePublicKey,
                                                const std::string& outputTorrentPath,
                                                const std::string& outputTorrentFileExtension )
{
    if ( fs::is_directory(pathToFile) )
    {
        throw std::runtime_error( std::string("moveFileToFlatDrive: 1-st parameter cannot be a folder: ") + pathToFile );
    }

    if ( !outputTorrentPath.empty() && !fs::is_directory(outputTorrentPath) )
    {
        throw std::runtime_error( std::string("moveFileToFlatDrive: 'outputTorrentPath' must be a folder: ") + pathToFile );
    }

    // setup file storage
    lt::file_storage fStorage;
    lt::add_files( fStorage, pathToFile, lt::create_flags_t{} );

    // create torrent info
    lt::create_torrent createInfo( fStorage, PIECE_SIZE, lt::create_torrent::v2_only );

    // calculate hashes
    lt::error_code ec;
    lt::set_piece_hashes( createInfo, fs::path(pathToFile).parent_path() );
    if ( ec )
    {
        throw std::runtime_error( std::string("moveFileToFlatDrive: libtorrent error: ") + ec.message() );
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
        std::ofstream fileStream( fs::path(outputTorrentPath) / (newFileName + outputTorrentFileExtension), std::ios::binary );
        fileStream.write( torrentFileBytes2.data(), torrentFileBytes2.size() );
    }

    return infoHash;
}

InfoHash calculateInfoHash( const std::string& pathToFile, const Key& drivePublicKey )
{
    if ( fs::is_directory(pathToFile) )
    {
        throw std::runtime_error( std::string("moveFileToFlatDrive: 1-st parameter cannot be a folder: ") + pathToFile );
    }

    // setup file storage
    lt::file_storage fStorage;
    lt::add_files( fStorage, pathToFile, lt::create_flags_t{} );

    // create torrent info
    lt::create_torrent createInfo( fStorage, PIECE_SIZE, lt::create_torrent::v2_only );

    // calculate hashes
    lt::error_code ec;
    lt::set_piece_hashes( createInfo, fs::path(pathToFile).parent_path() );
    if ( ec )
    {
        throw std::runtime_error( std::string("moveFileToFlatDrive: libtorrent error: ") + ec.message() );
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
// createDefaultLibTorrentSession
//

std::shared_ptr<Session> createDefaultSession( boost::asio::io_context&             context,
                                               std::string                          address,
                                               const LibTorrentErrorHandler&        alertHandler,
                                               std::weak_ptr<ReplicatorInt>         replicator,
                                               std::weak_ptr<lt::session_delegate>  downloadLimiter,
                                               const endpoint_list&                 bootstraps,
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

std::shared_ptr<Session> createDefaultSession( std::string                          address,
                                               const LibTorrentErrorHandler&        alertHandler,
                                               std::weak_ptr<lt::session_delegate>  downloadLimiter,
                                               const endpoint_list&                 bootstraps,
                                               bool                                 useTcpSocket)
{
    return std::make_shared<DefaultSession>( address, alertHandler, downloadLimiter, useTcpSocket, bootstraps );
}

}

/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "drive/Session.h"
#include "drive/Replicator.h"
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
#include <libtorrent/extensions/ut_metadata.hpp>
#include <libtorrent/aux_/generate_peer_id.hpp>
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/extensions.hpp"

#include <sirius_drive/session_delegate.h>

namespace fs = std::filesystem;

namespace sirius::drive {

enum { PIECE_SIZE = 16*1024 };

//
// DefaultSession
//
class DefaultSession: public Session, std::enable_shared_from_this<DefaultSession> {

    // Every drive have its own 'RemoveTorrentContext'
    //
    using RemoveContexts = std::vector<std::unique_ptr<RemoveTorrentContext>>;

    // This map is used to inform 'client' about downloading progress
    // Torrent id (uint32_t) is used instead of lt::torrent_handler
    //
    struct DownloadMapCell {
        fs::path                        m_saveFolder;
        std::vector<DownloadContext>    m_contexts;
    };

    using DownloadMap      = std::map<std::uint32_t,DownloadMapCell>;

private:

    // Endpoint of libtorrent node
    //
    std::string             m_addressAndPort;

    // Libtorrent session
    //
    lt::session             m_session;

    // Truncated drive public keys
    std::set<lt::sha1_hash> m_truncatedDrivePublicKeys;

    // see comments to 'RemoveSets'
    //
    RemoveContexts          m_removeContexts;
    std::mutex              m_removeMutex;

    // see coments to 'DownloadMap'
    //
    DownloadMap             m_downloadMap;
    std::mutex              m_downloadMapMutex;

    // It will be called on socket listening error
    //
    LibTorrentErrorHandler  m_alertHandler;

    std::weak_ptr<Replicator>           m_replicator;
    std::weak_ptr<lt::session_delegate> m_downloadLimiter;
    
    std::string             m_dbgOurPeerName = "";
    //int                     m_dbgPieceCounter = 0;

public:
    
    // Constructor for Replicator
    //
    DefaultSession( boost::asio::io_context&             context,
                    std::string                          address,
                    const LibTorrentErrorHandler&        alertHandler,
                    std::weak_ptr<Replicator>            replicator,
                    std::weak_ptr<lt::session_delegate>  downloadLimiter,
                    bool                                 useTcpSocket,
                    const endpoint_list&                 bootstraps
                    )
        : m_addressAndPort(address)
        , m_session( lt::session_params{ generateSessionSettings( useTcpSocket, bootstraps) }, context, {})
        , m_alertHandler(alertHandler)
        , m_replicator(replicator)
        , m_downloadLimiter(downloadLimiter)
    {
        m_dbgOurPeerName = m_downloadLimiter.lock()->dbgOurPeerName();
        
        continueSessionCreation();
        m_session.setDelegate( m_downloadLimiter );
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
        : m_addressAndPort(address)
        , m_session( lt::session_params{ generateSessionSettings( useTcpSocket, bootstraps ) } )
        , m_alertHandler(alertHandler)
        , m_downloadLimiter(downloadLimiter)
    {
        if ( downloadLimiter.lock() )
            m_dbgOurPeerName = downloadLimiter.lock()->dbgOurPeerName();
        
        continueSessionCreation();
        m_session.setDelegate( m_downloadLimiter );
        _LOG( "DefaultSession created: " << m_addressAndPort );
    }

    virtual ~DefaultSession() {
        LOG( "DefaultSession deleted" );
    }

    // for dbg
    lt::session &lt_session() override
    {
        return m_session;
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
        return settingsPack;
    }

    // createSession
    void continueSessionCreation() {
        m_session.set_alert_notify( [this] { alertHandler(); } );
        addDhtRequestPlugin();
    }

    virtual void endSession() override {
        //m_downloadMap.clear();
        //TODO?
        //m_session.abort();
    }

    virtual void removeTorrentsFromSession( const std::set<lt::torrent_handle>&  torrents,
                                            std::function<void()>                endNotification ) override
    {
        std::lock_guard locker( m_removeMutex );
        auto toBeRemoved = std::set<lt::torrent_handle>();

        _LOG( ":removeTorrentsFromSession: " << torrents.size() )

        for( const auto& torrentHandle : torrents )
        {
//            _LOG( ":remove_torrent: " << torrentHandle.info_hashes().v2 << " " << torrentHandle.status().state << " " << lt::torrent_status::seeding )
//            _LOG(":remove_torrent(2): " << torrentHandle.info_hashes().v2 << " " << torrentHandle.status().state )
//            assert(torrentHandle.is_valid());
            if ( !torrentHandle.is_valid() )
            {
                _LOG_ERR("TRY REMOVE NOT VALID TORRENT");
            }
            else
            {
                if ( torrentHandle.status().state > 2 ) // torrentHandle.status().state == lt::torrent_status::seeding )
                {
                    toBeRemoved.insert(torrentHandle);
                }
                //            else
                //            {
                //                _LOG( m_addressAndPort << "********:remove_torrent(3): " << torrentHandle.info_hashes().v2 << " " << torrentHandle.status().state )
                //            }
                m_session.remove_torrent( torrentHandle, lt::session::delete_partfile );
            }
        }
        
        if ( !toBeRemoved.empty() )
        {
            m_removeContexts.push_back( std::make_unique<RemoveTorrentContext>( toBeRemoved, endNotification ) );
        }
        else
        {
            boost::asio::post(lt_session().get_context(), [=] {
                endNotification();
            });
        }
    }

    virtual lt_handle addTorrentFileToSession( const std::string& torrentFilename,
                                               const std::string& folderWhereFileIsLocated,
                                               uint32_t           siriusFlags,
                                               endpoint_list list ) override {

        // read torrent file
        std::ifstream torrentFile( torrentFilename );
        std::vector<char> buffer( (std::istreambuf_iterator<char>(torrentFile)), std::istreambuf_iterator<char>() );

        // create add_torrent_params
        lt::add_torrent_params params;
        params.flags &= ~lt::torrent_flags::paused;
        params.flags &= ~lt::torrent_flags::auto_managed;

        //todo?
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

        //TODO!!!
        //LOG( "torrentFilename: " << torrentFilename );
//        if ( fs::path(torrentFilename).filename() == "d6c5a005e980b0d18c9f73fbf4b5123371807be9e0fc98f42cf1ac40081e7886" )
//        {
//            tHandle.set_upload_limit(100000);
//            tHandle.set_download_limit(100000);
//        }

//        connectPeers( tHandle, list );

        return tHandle;
    }

    InfoHash addActionListToSession( const ActionList& actionList,
                                     const Key& clientPublicKey,
                                     const std::string& workFolder,
                                     endpoint_list list ) override {

        // path to the data to be loaded into the replicator
        fs::path sandboxFolder = fs::path(workFolder) / "client-data";

        // clear tmpFolder
        std::error_code ec;
        fs::remove_all( sandboxFolder, ec );
        fs::create_directories( sandboxFolder );

        // path to root folder
        fs::path addFilesFolder = fs::path(sandboxFolder).append( "drive" );
        //fs::create_directory( addFilesFolder );

        // parse action list
        for( auto& action : actionList )
        {
            switch ( action.m_actionId )
            {
                case action_list_id::upload:
                {
                    fs::path sandboxFilePath = addFilesFolder/action.m_param2;
                    fs::create_directories( sandboxFilePath.parent_path() );
                    try
                    {
                        fs::create_symlink( action.m_param1, sandboxFilePath);
                    }
                    catch(...)
                    {
                        // ignore duplicate destination links
                    }
                    //fs::copy( action.m_param1, addFilesFolder/action.m_param2 );
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

        //if ( !fs::exists( addFilesFolder ) )
        {
            // it seems that it is a bug of libtorrent
            // when actionList.bin is a single file
            fs::path ballast = fs::path(sandboxFolder) / "ballast";
            std::ofstream file( ballast );
            std::string fileText = "ballast";
            file.write( fileText.c_str(), fileText.size() );
        }

        // save ActionList
        actionList.serialize( fs::path(sandboxFolder)/"actionList.bin" );

        // create torrent file
        InfoHash infoHash = createTorrentFile( fs::path(sandboxFolder), clientPublicKey, workFolder, fs::path(sandboxFolder)/"root.torrent" );

        // add torrent file
        addTorrentFileToSession( fs::path(sandboxFolder)/"root.torrent",
                                 workFolder,
                                 lt::sf_has_modify_data,
                                 list );

        return infoHash;
    }

    // downloadFile
    virtual lt_handle download( DownloadContext&&          downloadContext,
                                const std::string&         tmpFolder,
                                const ReplicatorList&      keysHints,
                                const endpoint_list&       endpointsHints) override {

        // create add_torrent_params
        lt::error_code ec;
        lt::add_torrent_params params = lt::parse_magnet_uri( magnetLink(downloadContext.m_infoHash), ec );
        if (ec) {
            throw std::runtime_error( std::string("downloadFile error: ") + ec.message() );
        }

        // where the file will be placed
        params.save_path = tmpFolder;

        params.m_transactionHash = downloadContext.m_transactionHash.array();
        params.m_downloadLimit   = downloadContext.m_downloadLimit;
        
        if ( downloadContext.m_downloadType == DownloadContext::client_data || downloadContext.m_downloadType == DownloadContext::missing_files )
            params.m_siriusFlags     = lt::sf_is_replicator | lt::sf_is_receiver;
        else
            params.m_siriusFlags     = lt::sf_is_receiver;

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

        // save download handler
        std::lock_guard locker(m_downloadMapMutex);

        if ( auto it = m_downloadMap.find(tHandle.id()); it != m_downloadMap.end() )
        {
            if ( downloadContext.m_downloadType == DownloadContext::file_from_drive )
            {
                if ( downloadContext.m_saveAs.empty() )
                    throw std::runtime_error("download(file_from_drive): DownloadContext::m_saveAs' is empty" );

                it->second.m_contexts.emplace_back( downloadContext );
            }
            else
            {
                LOG("download: already downloading type=" << downloadContext.m_downloadType
                    << " infoHash=" << toString(downloadContext.m_infoHash) );

                if ( downloadContext.m_downloadType == DownloadContext::fs_tree )
                    throw std::runtime_error("download(fs_tree): already downloading" );

                if ( downloadContext.m_downloadType == DownloadContext::client_data )
                    throw std::runtime_error("download(client_data): already downloading" );

                if ( downloadContext.m_downloadType == DownloadContext::missing_files )
                    throw std::runtime_error("download(missing_files): already downloading" );
            }
        }
        else
        {
            if ( downloadContext.m_downloadType == DownloadContext::fs_tree ) {
                downloadContext.m_saveAs = fs::path(tmpFolder) / FS_TREE_FILE_NAME;
            }
            else if ( downloadContext.m_downloadType == DownloadContext::file_from_drive ) {
                if (downloadContext.m_saveAs.empty())
                    throw std::runtime_error("download(file_from_drive): DownloadContext::m_saveAs' is empty");
            }

            LOG( "m_downloadMap.insert: " << tHandle.id() << " " << downloadContext.m_infoHash )
            m_downloadMap[ tHandle.id() ] = DownloadMapCell{ tmpFolder, {std::move(downloadContext)} };
        }
        
        return tHandle;
    }
    
    void removeDownloadContext( lt::torrent_handle tHandle ) override
    {
        m_downloadMap.erase( tHandle.id() );
    }


//    void loadTorrent( const InfoHash& infoHash,
//                           std::function<void(bool)> addTorrentNotifier,
//                           const std::string& torrentFilename,
//                           const std::string& savePath,
//                           endpoint_list endpointList ) override
//    {
//        if ( auto it = m_loadTorrentMap.find(infoHash); it != m_loadTorrentMap.end() )
//        {
//            //shift timer
//            return;
//        }
//        m_loadTorrentMap[infoHash] = addTorrentNotifier;
//        //start timer
//        addTorrentFileToSession( torrentFilename, savePath, endpointList );
//    };


    void connectPeers( lt::torrent_handle tHandle, endpoint_list list ) {

        if ( !m_session.is_valid() )
            throw std::runtime_error("connectPeers: libtorrent session is not valid");

        //TODO check if not set m_lastTorrentFileHandle
        for( const auto& endpoint : list ) {
            tHandle.connect_peer(endpoint);
        }
    }

    void      printActiveTorrents() override
    {
        LOG( "Active torrents:" );
        std::vector<lt::torrent_handle> torrents = m_session.get_torrents();
        for( const lt::torrent_handle& tHandle : torrents )
        {
//            if ( tHandle.is_valid() )
            if ( tHandle.in_session() )
            {
                LOG( " file hash: " << tHandle.info_hashes().v2 );
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
        std::weak_ptr<Replicator> m_replicator;
        DhtRequestPlugin( std::weak_ptr<Replicator> replicator ) : m_replicator(replicator) {}

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

            if ( query == "get_peers" || query == "announce_peer" )
            {
                //(???) NULL dht_direct_response_alert?
                return false;
            }

//            _LOG( "message: " << message );
//            _LOG( "response: " << response );

            const std::set<lt::string_view> supportedQueries =
                    { "opinion", "dn_opinion", "code_verify", "verify_opinion", "handshake", "endpoint_request", "endpoint_response" };
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
                auto str = message.dict_find_string_value("x");
                std::string packet( (char*)str.data(), (char*)str.data()+str.size() );

                if ( auto replicator = m_replicator.lock(); replicator )
                {
                    std::array<uint8_t,32> driveKey;
                    std::array<uint8_t,32> downloadChannelHash;
                    uint8_t* ptr = (uint8_t*)str.data();
                    memcpy( driveKey.data(), ptr, driveKey.size() );
                    ptr += driveKey.size();
                    memcpy( downloadChannelHash.data(), ptr, downloadChannelHash.size() );

                    DownloadOpinion opinion;
                    bool opinionExists = replicator->createSyncOpinion( driveKey, downloadChannelHash, opinion );

                    std::ostringstream os( std::ios::binary );
                    cereal::PortableBinaryOutputArchive archive( os );

                    if ( opinionExists )
                    {
                        archive( uint8_t(1) );
                        archive( driveKey );
                        archive( downloadChannelHash );
                        archive( opinion );
                    }
                    else
                    {
                        archive( uint8_t(0) );
                    }

                    std::string m_dbgOurPeerName;
                    _LOG( "response[r][q]: " << query );
                    response["r"]["q"] = std::string(query);
                    response["r"]["ret"] = os.str();
                    return true;
                }
            }
                
            else if ( query == "rcpt" )
//            else if ( message.dict_find_string_value("q") == "rcpt" )
            {
                auto str = message.dict_find_string_value("x");

                std::array<uint8_t,32>  downloadChannelId;
                std::array<uint8_t,32>  clientPublicKey;
                std::array<uint8_t,32>  replicatorPublicKey;
                uint64_t                totalDownloadedSize;
                Signature               signature;
                
                auto messageSize = downloadChannelId.size() + clientPublicKey.size() + replicatorPublicKey.size() + sizeof(totalDownloadedSize) + signature.size();

                // skip bad size messages
                if ( str.size() != messageSize )
                    return false;
                
                uint8_t* ptr = (uint8_t*)str.data();
                memcpy( downloadChannelId.data(), ptr, downloadChannelId.size() );
                ptr += downloadChannelId.size();
                memcpy( clientPublicKey.data(), ptr, clientPublicKey.size() );
                ptr += clientPublicKey.size();
                memcpy( replicatorPublicKey.data(), ptr, clientPublicKey.size() );
                ptr += replicatorPublicKey.size();
                memcpy( &totalDownloadedSize, ptr, 8 );
                ptr += 8;
                memcpy( signature.data(), ptr, signature.size() );
                //ptr += signature.size();

                // it will be verifyed in acceptReceiptFromAnotherReplicator()
                //assert( m_replicator->verifyReceipt( downloadChannelId, clientPublicKey, replicatorPublicKey, totalDownloadedSize, signature.array() ) );

                if ( auto replicator = m_replicator.lock(); replicator )
                {
                    replicator->acceptReceiptFromAnotherReplicator( downloadChannelId,
                                                                    clientPublicKey,
                                                                    replicatorPublicKey,
                                                                    totalDownloadedSize,
                                                                    signature.array() );
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

    void  sendMessage( boost::asio::ip::udp::endpoint udp, const std::vector<uint8_t>& ) override
    {
        lt::entry e;
        e["q"] = "sirius_message";
        e["x"] = "-----------------------------------------------------------";
        LOG( "lt::entry e: " << e );

//        lt::client_data_t client_data(this);
        m_session.dht_direct_request( udp, e, lt::client_data_t(reinterpret_cast<int*>(12345))  );
    }

    void sendMessage( const std::string& query, boost::asio::ip::udp::endpoint endPoint, const std::vector<uint8_t>& message, void* userdata ) override
    {
        lt::entry entry;
        entry["q"] = query;
        entry["x"] = std::string( message.begin(), message.end() );

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

    void handleResponse( lt::bdecode_node response )
    {
        try
        {
            //auto r = response.dict_find_string_value("r");
            auto rDict = response.dict_find_dict("r");
            auto query = rDict.dict_find_string_value("q");
            auto retString = rDict.dict_find_string_value("ret");
            if ( query == "get_dn_rcpts" )
            {
                m_replicator.lock()->onSyncDnOpinionReceived( std::string( retString.begin(), retString.end() ) );
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

    void alertHandler() {

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
//                    _LOG( "debug_alert:" << m_addressAndPort << ": " <<  theAlert->message() );
//                    break;
//                }

//                case lt::peer_log_alert::alert_type: {
//                    _LOG(  ": peer_log_alert: " << alert->message())
//                    break;
//                }
//
//                case lt::log_alert::alert_type: {
//                    _LOG(  ": session_log_alert: " << alert->message())
//                    break;
//                }

                case lt::dht_bootstrap_alert::alert_type: {
                    __LOG( "dht_bootstrap_alert: " << alert->message() )
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

                case lt::add_torrent_alert::        alert_type: {
                    auto* theAlert = dynamic_cast<lt::add_torrent_alert*>(alert);
                    _LOG("*** add_torrent_alert: " << theAlert->message());
                }
//                case lt::dht_announce_alert::       alert_type:
                //case lt::torrent_log_alert::        alert_type:
                case lt::incoming_connection_alert::alert_type: {
//                    LOG( m_addressAndPort << " " << alert->what() << ":("<< alert->type() <<")  " << alert->message() );
                    break;
                }

                case lt::dht_direct_response_alert::alert_type: {
                     auto* theAlert = dynamic_cast<lt::dht_direct_response_alert*>(alert);
                    _LOG( "*** dht_direct_response_alert: " );
                     auto response = theAlert->response();
                     if ( response.type() == lt::bdecode_node::dict_t )
                     {
                         auto rDict = response.dict_find_dict("r");
                         if ( rDict.type() == lt::bdecode_node::dict_t )
                         {
                             auto query = rDict.dict_find_string_value("q");
                             _LOG( "dht_query: " << query )
                             if ( query == "get_dn_rcpts" )
                             {
                                 handleResponse( response);
                             }
                         }
                     }
                    else
                    {
                        _LOG_WARN( "*** NULL dht_direct_response_alert: " << theAlert->what() << ":("<< alert->type() <<")  " << theAlert->message() );
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
//#ifdef __APPLE__
//#pragma mark --download-progress--
//#endif
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
//
////todo++++                            if ( auto it =  m_downloadMap.find(theAlert->handle.id());
////                                      it != m_downloadMap.end() )
////                            {
////                                for( auto& context : it->second.m_contexts )
////                                {
////                                    context.m_downloadNotification( download_status::code::downloading,
////                                                                    context.m_infoHash,
////                                                                    context.m_saveAs,
////                                                                    fp[i],
////                                                                    fsize,
////                                                                    "" );
////                                }
////                            }
//
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
////
////                            auto it = m_downloadMap.find(theAlert->handle.id());
////
////                            if ( it != m_downloadMap.end() )
////                            {
////                                // get peers info
////                                std::vector<lt::peer_info> peers;
////                                theAlert->handle.get_peer_info(peers);
////
////                                for (const lt::peer_info &pi : peers)
////                                {
////                                    //_LOG("Peer ip: " << pi.ip.address().to_string())
////                                    //_LOG("Peer id: " << pi.pid.to_string())
////
////                                    // the total number of bytes downloaded from and uploaded to this peer.
////                                    // These numbers do not include the protocol chatter, but only the
////                                    // payload data.
////                                    _LOG( m_addressAndPort << ": " << m_downloadLimiter->dbgOurPeerName() << " Total downloaded: " << pi.total_download << " from: " << pi.ip.address().to_string() )
////                                    _LOG( m_addressAndPort << ": " << "Total uploaded: " << pi.total_upload)
////                                }
////
////                                // remove torrent
////                                //todo++++
////                                m_session.remove_torrent( theAlert->handle, lt::session::delete_partfile );
////                            }
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

                    LOG(  m_addressAndPort << ": ERROR!!!: torrent error: " << theAlert->message())

                    if ( auto downloadConextIt  = m_downloadMap.find(theAlert->handle.id());
                              downloadConextIt != m_downloadMap.end() )
                    {
                        const auto contexts = downloadConextIt->second.m_contexts;

                        // remove entry from downloadHandlerMap
                        std::lock_guard<std::mutex> locker(m_downloadMapMutex);
                        m_downloadMap.erase( downloadConextIt->first );

                        for( const auto& context : contexts )
                        {
                            context.m_downloadNotification( download_status::code::failed,
                                                            context.m_infoHash,
                                                            context.m_saveAs,
                                                            0,
                                                            0,
                                                            "" );

                        }
                    }

                    break;
                }

                case lt::torrent_finished_alert::alert_type: {
                    auto *theAlert = dynamic_cast<lt::torrent_finished_alert*>(alert);
                    _LOG( "*** torrent_finished_alert:" << theAlert->handle.torrent_file()->files().file_path(0) );
//                    _LOG( "*** lt::torrent_finished_alert:" << m_downloadLimiter->dbgOurPeerName() << " " << theAlert->handle.info_hashes().v2 );
                    //TODO save torrent file???
                    //lt::torrent_status st = theAlert->handle.status( lt::torrent_handle::query_torrent_file );

                    if ( auto it = m_downloadMap.find(theAlert->handle.id()); it != m_downloadMap.end() )
                    {
                        _LOG( " *** torrent_finished: " << theAlert->handle.info_hashes().v2 )

                        // get peers info
                        std::vector<lt::peer_info> peers;
                        theAlert->handle.get_peer_info(peers);

//                        for (const lt::peer_info &pi : peers)
//                        {
//                            //_LOG("Peer ip: " << pi.ip.address().to_string())
//                            //_LOG("Peer id: " << pi.pid.to_string())
//
//                            // the total number of bytes downloaded from and uploaded to this peer.
//                            // These numbers do not include the protocol chatter, but only the
//                            // payload data.
////                            _LOG( m_addressAndPort << ": " << m_downloadLimiter->dbgOurPeerName() << " Total downloaded: " << pi.total_download << " from: " << pi.ip.address().to_string() )
////                            _LOG( m_addressAndPort << ": " << "Total uploaded: " << pi.total_upload)
//                        }

                        // remove torrent
                        //todo++++
                        m_session.remove_torrent( theAlert->handle, lt::session::delete_partfile );
                    }
                    break;
                }
                    
                case lt::torrent_deleted_alert::alert_type: {
                    auto *theAlert = dynamic_cast<lt::torrent_deleted_alert*>(alert);
                    _LOG( " *** torrent_deleted_alert:" << theAlert->handle.info_hashes().v2 );
                    _LOG( " *** torrent_deleted_alert:" << theAlert->handle.torrent_file()->files().file_name(0) );
                    //LOG( "*** torrent_deleted_alert:" << theAlert->handle.torrent_file()->files().file_path(0) );
                    //LOG( "*** get_torrents().size()=" << m_session.get_torrents().size() );

                    // Notify about removed torrents
                    //
                    {
                        std::lock_guard<std::mutex> locker(m_removeMutex);
                        _LOG( " *** torrent_deleted_alert: removeContext.Size: " << m_removeContexts.size() );

                        // loop by set
                        for ( auto removeContextIt  = m_removeContexts.begin(); removeContextIt != m_removeContexts.end(); )
                        {
                            auto& removeContext = *removeContextIt->get();

                            _LOG( m_addressAndPort << "  removeContext.Size" << m_removeContexts.size() );

                            // skip not-ordered torrents
                            if ( auto torrentIt  = removeContext.m_torrentSet.find(theAlert->handle);
                                      torrentIt != removeContext.m_torrentSet.end() )
                            {
                                // remove torrent from 'context'
                                removeContext.m_torrentSet.erase(torrentIt);

                                //TODO?
                                //std::erase_if( removeContext.m_torrentSet,
                                //               [](auto const& torrent) { return !torrent.is_valid(); } );

                                // try to remove 'context'
                                // todo calculate valid torrents!
                                _LOG( m_addressAndPort << " removeContext.m_torrentSet.size=" << removeContext.m_torrentSet.size() );
                                if ( removeContext.m_torrentSet.empty() )
                                {
                                    auto endRemoveNotification = removeContext.m_endRemoveNotification;
                                    m_removeContexts.erase( removeContextIt );
                                    _LOG( m_addressAndPort << " endRemoveNotification() called");
                                    endRemoveNotification();
                                }
                                else
                                {
                                    removeContextIt++;
                                }
                            }
                        }
                    }

                    // Notify about completed downloads
                    //

                    _LOG( " *** torrent_deleted_alert: m_downloadMap.Size: " << m_downloadMap.size() << " : " << theAlert->handle.id() );
                    if ( auto it =  m_downloadMap.find(theAlert->handle.id());
                              it != m_downloadMap.end() )
                    {
                        auto& contextVector = it->second.m_contexts;

                        if ( contextVector.empty() )
                        {
                            _LOG( "Internal error: " << contextVector.empty() );
                        }
                        else
                        {
                            fs::path srcFilePath = fs::path(it->second.m_saveFolder) /
                                                        hashToFileName(contextVector[0].m_infoHash);

                            //_LOG( "contextVector.size(): " << contextVector.size() );
                            for( size_t i=0; i<contextVector.size(); i++ )
                            {
                                auto& context = contextVector[i];

                                if ( !context.m_saveAs.empty() && context.m_downloadType == DownloadContext::file_from_drive )
                                {
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
                                                                0,
                                                                0,
                                                                "" );
                            }
                        }

                        // remove entry from downloadHandlerMap
                        std::lock_guard<std::mutex> locker(m_downloadMapMutex);
                        m_downloadMap.erase( it->first );

                    }

//                    if ( auto downloadConextIt  = m_downloadMap.find(theAlert->handle.id());
//                              downloadConextIt != m_downloadMap.end() )
//                    {
//                        auto contexts = downloadConextIt->second;

//                        // remove entry from downloadHandlerMap
//                        std::lock_guard<std::mutex> locker(m_downloadMapMutex);
//                        m_downloadMap.erase( downloadConextIt->first );

//                        for( auto& context : contexts )
//                        {
//                            context.m_downloadNotification( context, download_status::complete, 100.0, "" );
//                        }
//                    }

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
                            const Key&         drivePublicKey, // or client public key
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
        LOG( "createTorrentFile error: " << ec );
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
                                                const Key&         drivePublicKey, // or client public key
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

//
// createDefaultLibTorrentSession
//

std::shared_ptr<Session> createDefaultSession( boost::asio::io_context&             context,
                                               std::string                          address,
                                               const LibTorrentErrorHandler&        alertHandler,
                                               std::weak_ptr<Replicator>            replicator,
                                               std::weak_ptr<lt::session_delegate>  downloadLimiter,
                                               const endpoint_list&                 bootstraps,
                                               bool                                 useTcpSocket)
{
    return std::make_shared<DefaultSession>( context, address, alertHandler, replicator, downloadLimiter, useTcpSocket, bootstraps );
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

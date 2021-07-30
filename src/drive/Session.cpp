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

#ifdef SIRIUS_DRIVE_MULTI
#include <sirius_drive/session_delegate.h>
#endif

// boost
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>


namespace fs = std::filesystem;

namespace sirius { namespace drive {

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
    //using LoadTorrentMap   = std::map<InfoHash,std::function<void(bool)>>;

private:

    // Endpoint of libtorrent node
    //
    std::string             m_addressAndPort;

    // Libtorrent session
    //
    lt::session             m_session;

    // see comments to 'RemoveSets'
    //
    RemoveContexts          m_removeContexts;
    std::mutex              m_removeMutex;

    // see coments to 'DownloadMap'
    //
    DownloadMap             m_downloadMap;
    std::mutex              m_downloadMapMutex;

    // loadTorrentMap
    //LoadTorrentMap          m_loadTorrentMap;

    // It will be called on socket listening error
    //
    LibTorrentErrorHandler  m_alertHandler;

#ifdef SIRIUS_DRIVE_MULTI
    std::shared_ptr<lt::session_delegate> m_downloadLimiter;
#endif

        public:

    DefaultSession( std::string address,
                    LibTorrentErrorHandler alertHandler
#ifdef SIRIUS_DRIVE_MULTI
                    ,std::weak_ptr<lt::session_delegate> downloadLimiter
#endif
                    )
        : m_addressAndPort(address), m_alertHandler(alertHandler)
#ifdef SIRIUS_DRIVE_MULTI
        , m_downloadLimiter(downloadLimiter)
#endif
    {
        createSession();
#ifdef SIRIUS_DRIVE_MULTI
        m_session.setDelegate( m_downloadLimiter );
#endif
        LOG( "DefaultSession created: " << m_addressAndPort );
    }

    virtual ~DefaultSession() {
        LOG( "DefaultSession deleted" );
    }

    // for dbg
    lt::session&  lt_session() override { return m_session; }


    // createSession
    void createSession() {

        lt::settings_pack settingsPack;

        settingsPack.set_int( lt::settings_pack::alert_mask, ~0 );//lt::alert_category::all );
        settingsPack.set_str( lt::settings_pack::dht_bootstrap_nodes, "" );

        // todo public_key?
        char todoPubKey[32];
        std::memset(todoPubKey,'x', sizeof(todoPubKey));
        todoPubKey[5] = 0;
        settingsPack.set_str(  lt::settings_pack::user_agent, std::string(todoPubKey,32) );

        settingsPack.set_bool( lt::settings_pack::enable_outgoing_utp, false );
        settingsPack.set_bool( lt::settings_pack::enable_incoming_utp, false );

        settingsPack.set_bool( lt::settings_pack::enable_outgoing_tcp, true );
        settingsPack.set_bool( lt::settings_pack::enable_incoming_tcp, true );

        settingsPack.set_bool( lt::settings_pack::enable_dht, true );
        settingsPack.set_bool( lt::settings_pack::enable_lsd, false ); // is it needed?
        settingsPack.set_bool( lt::settings_pack::enable_upnp, false );
        settingsPack.set_str(  lt::settings_pack::dht_bootstrap_nodes, "" );

        settingsPack.set_str(  lt::settings_pack::listen_interfaces, m_addressAndPort );
        settingsPack.set_bool( lt::settings_pack::allow_multiple_connections_per_ip, true );

        m_session.apply_settings(settingsPack);
        m_session.set_alert_notify( [this] { alertHandler(); } );

        addDhtRequestPlugin();
    }

    virtual void endSession() override {
        m_downloadMap.clear();
        //TODO?
        m_session.abort();
    }

    virtual void removeTorrentsFromSession( std::set<lt::torrent_handle>&& torrents,
                                            std::function<void()>          endNotification ) override
    {
        {
            std::lock_guard locker( m_removeMutex );
            m_removeContexts.push_back( std::make_unique<RemoveTorrentContext>( std::move(torrents), endNotification ) );
        }

        for( const auto& torrentHandle : torrents )
        {
            //LOG( "remove_torrent: " << torrentHandle.info_hashes().v2 )
            m_session.remove_torrent( torrentHandle, lt::session::delete_partfile );
        }
    }

    virtual lt_handle addTorrentFileToSession( const std::string& torrentFilename,
                                          const std::string& savePath,
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

        params.storage_mode = lt::storage_mode_sparse;
        params.save_path = fs::path(savePath);
        params.ti = std::make_shared<lt::torrent_info>( buffer, lt::from_span );

        //dbg///////////////////////////////////////////////////
        auto tInfo = lt::torrent_info(buffer, lt::from_span);
        LOG( "addTorrentToSession: " << tInfo.info_hashes().v2 );
//        LOG( tInfo.info_hashes().v2 ) );
//        LOG( "add torrent: torrent filename:" << torrentFilename );
//        LOG( "add torrent: fileFolder:" << fileFolder );
        //LOG( "add torrent: " << lt::make_magnet_uri(tInfo) );
        //dbg///////////////////////////////////////////////////

        lt::torrent_handle tHandle = m_session.add_torrent(params);

        //TODO!!!
        //LOG( "torrentFilename: " << torrentFilename );
//        if ( fs::path(torrentFilename).filename() == "d6c5a005e980b0d18c9f73fbf4b5123371807be9e0fc98f42cf1ac40081e7886" )
//        {
//            tHandle.set_upload_limit(100000);
//            tHandle.set_download_limit(100000);
//        }

        connectPeers( tHandle, list );

        return tHandle;
    }

    InfoHash addActionListToSession( const ActionList& actionList,
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
                    fs::create_symlink( action.m_param1, sandboxFilePath);
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
        InfoHash infoHash = createTorrentFile( fs::path(sandboxFolder), workFolder, fs::path(sandboxFolder)/"root.torrent" );

        // add torrent file
        addTorrentFileToSession( fs::path(sandboxFolder)/"root.torrent", workFolder, list );

        return infoHash;
    }

    // downloadFile
    virtual void download( DownloadContext&&    downloadContext,
                           const std::string&   tmpFolder,
                           endpoint_list        list  ) override {

        // create add_torrent_params
        lt::error_code ec;
        lt::add_torrent_params params = lt::parse_magnet_uri( magnetLink(downloadContext.m_infoHash), ec );
        if (ec) {
            throw std::runtime_error( std::string("downloadFile error: ") + ec.message() );
        }

        // where the file will be placed
        params.save_path = tmpFolder;

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
        for( auto endpoint : list ) {
            LOG( "connect_peer: " << endpoint.address() << ":" << endpoint.port() );
            tHandle.connect_peer(endpoint);
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

            m_downloadMap[ tHandle.id() ] = DownloadMapCell{ tmpFolder, {std::move(downloadContext)} };
        }
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
        for( auto endpoint : list ) {
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
                auto hash = tHandle.info_hashes().v2;
                LOG( " file hash: " << hash );
            }
        }
    }

#pragma mark --messaging--
    struct DhtRequestPlugin : lt::plugin
    {
        std::weak_ptr<Replicator> m_replicator;
        DhtRequestPlugin() {}

        feature_flags_t implemented_features() override
        {
            return plugin::dht_request_feature;
        }

        bool on_dht_request(
            lt::string_view                         query,
            boost::asio::ip::udp::endpoint const&   /*source*/,
            lt::bdecode_node const&                 message,
            lt::entry&                              /*response*/ ) override
        {
            if ( query == "get_peers" || query == "announce_peer" )
                return false;

            LOG( "query: " << query );
            LOG( "message: " << message );
//            if ( message.dict_find_string_value("q") == "sirius_message" )
//            {
//                if ( message.dict_find_string_value("cmd") == "root_hash" )
//                {
//                    auto drivePublicKey = message.dict_find_string_value("drive");
//
//                    if ( auto replicator = m_replicator.lock(); replicator )
//                    {
//                        InfoHash hash = replicator->getRootHash( std::string(drivePublicKey) );
//                        response["root_hash"] = toString( hash );
//                    }
//                    return true;
//                }
//                response["x"] = "sirius_message_response";
//                return true;
//            }
            return true;
        }
    };

    void addDhtRequestPlugin()
    {
        m_session.add_extension(std::make_shared<DhtRequestPlugin>());
    }

    void  sendMessage( boost::asio::ip::udp::endpoint udp, const std::vector<uint8_t>& ) override
    {
        lt::entry e;
        e["q"] = "sirius_message";
        e["x"] = "-----------------------------------------------------------";
        LOG( "lt::entry e: " << e );

        lt::client_data_t client_data(this);
        m_session.dht_direct_request( udp, e );
    }

    void handleResponse( lt::bdecode_node response )
    {
        LOG( "lt::bdecode_node response: " << response );
        LOG( "lt::bdecode_node response: " << response );
    }

private:

    void alertHandler() {

        // extract alerts
        std::vector<lt::alert *> alerts;
        m_session.pop_alerts(&alerts);

        // loop by alerts
        for (auto &alert : alerts) {

//            if ( alert->type() != lt::log_alert::alert_type )
//            {
////                if ( m_addressAndPort == "192.168.1.102:5551" ) {
//                    LOG( ">" << m_addressAndPort << " " << alert->what() << ":("<< alert->type() <<")  " << alert->message() );
////                }
//            }

#pragma mark --alerts--
            switch (alert->type()) {
                case lt::add_torrent_alert::        alert_type:
                case lt::dht_announce_alert::       alert_type:
                //case lt::torrent_log_alert::        alert_type:
                case lt::incoming_connection_alert::alert_type: {
//                    LOG( m_addressAndPort << " " << alert->what() << ":("<< alert->type() <<")  " << alert->message() );
                    break;
                }

                case lt::dht_direct_response_alert::alert_type: {
                    auto* theAlert = dynamic_cast<lt::dht_direct_response_alert*>(alert);
                    auto response = theAlert->response();
                    handleResponse( response );
//                    LOG( m_addressAndPort << " " << theAlert->what() << ":("<< alert->type() <<")  " << alert->message() );
//                    LOG( m_addressAndPort << " " << response );
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

                    LOG( "#!!!peer_snubbed_alert!!!: " << theAlert->endpoint << "\n" );
                    break;
                }

                case lt::peer_disconnected_alert::alert_type: {
//                    auto* theAlert = dynamic_cast<lt::peer_disconnected_alert*>(alert);
//
//                    LOG( "#peer_disconnected_alert: " << theAlert->error.category().name() << " " << theAlert->error << " " << theAlert->endpoint << "\n" );
//                    break;
                }

                case lt::peer_log_alert::alert_type: {
                    //LOG(  m_addressAndPort << ": peer_log_alert: " << alert->message())
                    break;
                }

                // piece_finished_alert
                case lt::piece_finished_alert::alert_type: {

                    auto *theAlert = dynamic_cast<lt::piece_finished_alert *>(alert);

                    if ( theAlert ) {

                        // TODO: better to use piece_granularity
                        std::vector<int64_t> fp = theAlert->handle.file_progress();

                        // check completeness
                        bool isAllComplete = true;
                        for( uint32_t i=0; i<fp.size(); i++ ) {

                            auto fsize = theAlert->handle.torrent_file()->files().file_size(i);
                            bool const complete = ( fp[i] == fsize );

                            isAllComplete = isAllComplete && complete;

                            //dbg/////////////////////////
                            const std::string filePath = theAlert->handle.torrent_file()->files().file_path(i);
                            LOG( m_addressAndPort << ": " << filePath << ": alert: progress: " << fp[i] << " of " << fsize );
                            //dbg/////////////////////////

                            if ( auto it =  m_downloadMap.find(theAlert->handle.id());
                                      it != m_downloadMap.end() )
                            {
                                for( auto& context : it->second.m_contexts )
                                {
                                    context.m_downloadNotification( download_status::code::downloading,
                                                                    context.m_infoHash,
                                                                    context.m_saveAs,
                                                                    fp[i],
                                                                    fsize,
                                                                    "" );
                                }
                            }

                        }

                        if ( isAllComplete )
                        {
                            auto it = m_downloadMap.find(theAlert->handle.id());

                            if ( it != m_downloadMap.end() )
                            {
                                // get peers info
                                std::vector<lt::peer_info> peers;
                                theAlert->handle.get_peer_info(peers);

                                for (const lt::peer_info &pi : peers)
                                {
                                    LOG("Peer ip: " << pi.ip.address().to_string())
                                    LOG("Peer id: " << pi.pid.to_string())

                                    // the total number of bytes downloaded from and uploaded to this peer.
                                    // These numbers do not include the protocol chatter, but only the
                                    // payload data.
                                    LOG("Total download: " << pi.total_download)
                                    LOG("Total upload: " << pi.total_upload)
                                }

                                // remove torrent
                                m_session.remove_torrent( theAlert->handle, lt::session::delete_partfile );
                            }
                        }
                    }
                    break;
                }

                case lt::file_completed_alert::alert_type: {
//                    auto *theAlert = dynamic_cast<lt::file_completed_alert *>(alert);
//                    //LOG( "!!!!!! file_completed_alert !!!!" );

//                    auto it = m_downloadMap.find(theAlert->handle.id());

//                    if ( it != m_downloadMap.end() )
//                    {
//                        // get peers info
////                        std::vector<lt::peer_info> peers;
////                        theAlert->handle.get_peer_info(peers);

////                        for (const lt::peer_info &pi : peers)
////                        {
////                            LOG("Peer ip: " << pi.ip.address().to_string())
////                            LOG("Peer id: " << pi.pid.to_string())

////                            // the total number of bytes downloaded from and uploaded to this peer.
////                            // These numbers do not include the protocol chatter, but only the
////                            // payload data.
////                            LOG("Total download: " << pi.total_download)
////                            LOG("Total upload: " << pi.total_upload)
////                        }

//                        // remove torrent
//                        m_session.remove_torrent( theAlert->handle, lt::session::delete_partfile );
//                    }
                    break;
                }

                case lt::torrent_error_alert::alert_type: {
                    auto *theAlert = dynamic_cast<lt::torrent_error_alert *>(alert);

                    LOG(  m_addressAndPort << ": torrent error: " << theAlert->message())

                    if ( auto downloadConextIt  = m_downloadMap.find(theAlert->handle.id());
                              downloadConextIt != m_downloadMap.end() )
                    {
                        auto contexts = downloadConextIt->second.m_contexts;

                        // remove entry from downloadHandlerMap
                        std::lock_guard<std::mutex> locker(m_downloadMapMutex);
                        m_downloadMap.erase( downloadConextIt->first );

                        for( auto context : contexts )
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


                case lt::torrent_deleted_alert::alert_type: {
                    auto *theAlert = dynamic_cast<lt::torrent_deleted_alert*>(alert);
                    //LOG( "*** lt::torrent_deleted_alert:" << theAlert->handle.info_hashes().v2 );
                    //LOG( "*** lt::torrent_deleted_alert:" << theAlert->handle.torrent_file()->files().file_name(0) );
                    //LOG( "*** lt::torrent_deleted_alert:" << theAlert->handle.torrent_file()->files().file_path(0) );
                    //LOG( "*** get_torrents().size()=" << m_session.get_torrents().size() );

                    // Notify about removed torrents
                    //
                    {
                        std::lock_guard<std::mutex> locker(m_removeMutex);

                        // loop by set
                        for ( auto removeContextIt  = m_removeContexts.begin(); removeContextIt != m_removeContexts.end(); )
                        {
                            auto& removeContext = *removeContextIt->get();

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
                                if ( removeContext.m_torrentSet.empty() )
                                {
                                    auto endRemoveNotification = removeContext.m_endRemoveNotification;
                                    m_removeContexts.erase( removeContextIt );
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

                    if ( auto it =  m_downloadMap.find(theAlert->handle.id());
                              it != m_downloadMap.end() )
                    {
                        auto& contextVector = it->second.m_contexts;

                        if ( contextVector.empty() )
                        {
                            LOG( "Internal error: " << contextVector.empty() );
                        }
                        else
                        {
                            fs::path srcFilePath = fs::path(it->second.m_saveFolder) /
                                                        internalFileName(contextVector[0].m_infoHash);

                            for( size_t i=0; i<contextVector.size(); i++ )
                            {
                                auto& context = contextVector[i];

                                if ( !context.m_saveAs.empty() && context.m_downloadType == DownloadContext::file_from_drive )
                                {
                                    fs::path destFilePath = context.m_saveAs;

                                    if ( !fs::exists( destFilePath.parent_path() ) ) {
                                        fs::create_directories( destFilePath.parent_path() );
                                    }

                                    if ( fs::exists( destFilePath ) ) {
                                        fs::remove( destFilePath );
                                    }

                                    if ( i == contextVector.size()-1 )
                                    {
                                        fs::rename( srcFilePath, destFilePath );
                                    }
                                    else
                                    {
                                        fs::copy( srcFilePath, destFilePath );
                                    }
                                }

                                context.m_downloadNotification( download_status::code::complete,
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

//                        for( auto context : contexts )
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

                case lt::log_alert::alert_type: {
                    auto *theAlert = dynamic_cast<lt::file_error_alert *>(alert);

                    if ( theAlert ) {
                        LOG(  "log_alert: " << theAlert->message())
                    }
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
InfoHash createTorrentFile( const std::string& fileOrFolder, const std::string& rootFolder, const std::string& outputTorrentFilename )
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

InfoHash calculateInfoHashAndTorrent( const std::string& pathToFile,
                                      const std::string& drivePublicKey,
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
    entry_info["info"].dict()["sirius drive"]=lt::entry( drivePublicKey );

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
    std::string newFileName = internalFileName(infoHash);

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

// createDefaultLibTorrentSession
#ifdef SIRIUS_DRIVE_MULTI
std::shared_ptr<Session> createDefaultSession( std::string address,
                                               const LibTorrentErrorHandler& alertHandler,
                                               std::weak_ptr<lt::session_delegate> downloadLimiter )
{
    return std::make_shared<DefaultSession>( address, alertHandler, downloadLimiter );
}
#else
std::shared_ptr<Session> createDefaultSession( std::string address,
                                               const LibTorrentErrorHandler& alertHandler )
{
    return std::make_shared<DefaultSession>( address, alertHandler );
}
#endif

}}

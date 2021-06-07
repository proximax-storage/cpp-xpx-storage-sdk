/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "drive/Session.h"
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
#include <libtorrent/extensions/ut_metadata.hpp>

// boost
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>


namespace fs = std::filesystem;

namespace sirius { namespace drive {

//
// DefaultLibTorrentSession -
//
class DefaultSession: public Session {
    using RemoveTorrentSet = std::set<std::uint32_t>;
    using DownloadMap = std::map<std::uint32_t,DownloadHandler>;

    std::string             m_addressAndPort;

    lt::session             m_session;

    DownloadMap             m_downloadHandlerMap;
    RemoveTorrentSet        m_removeHandlerSet;
    RemoveHandler           m_removeHandler;

    LibTorrentErrorHandler  m_alertHandler;

public:

    DefaultSession( std::string address, LibTorrentErrorHandler alertHandler )
        : m_addressAndPort(address), m_alertHandler(alertHandler)
    {
        createSession();
    }

    virtual ~DefaultSession() {}

    // createSession
    void createSession() {

        lt::settings_pack settingsPack;

        settingsPack.set_int( lt::settings_pack::alert_mask, ~0 );//lt::alert_category::all );
        settingsPack.set_str( lt::settings_pack::dht_bootstrap_nodes, "" );

        boost::uuids::uuid uuid = boost::uuids::random_generator()();
        settingsPack.set_str(  lt::settings_pack::user_agent, boost::uuids::to_string(uuid) );

        settingsPack.set_bool( lt::settings_pack::enable_dht, true );
        settingsPack.set_bool( lt::settings_pack::enable_lsd, false ); // is it needed?
        settingsPack.set_str(  lt::settings_pack::listen_interfaces, m_addressAndPort );
        settingsPack.set_bool( lt::settings_pack::allow_multiple_connections_per_ip, true );

        m_session.apply_settings(settingsPack);
        m_session.set_alert_notify( [this] { alertHandler(); } );
    }

    virtual void endSession() override {
        m_downloadHandlerMap.clear();
        //TODO?
        m_session.abort();
    }

    void connectPeers( lt::torrent_handle tHandle, endpoint_list list ) {

        if ( !m_session.is_valid() )
            throw std::runtime_error("connectPeers: libtorrent session is not valid");

        //TODO check if not set m_lastTorrentFileHandle
        for( auto endpoint : list ) {
            tHandle.connect_peer(endpoint);
        }
    }

    virtual void removeTorrentFromSession( lt_handle torrentHandle, RemoveHandler removeHandler ) override {
        if ( m_removeHandlerSet.find(torrentHandle.id()) != m_removeHandlerSet.end() )
        {
            //TODO
            LOG_ERR( "double torrent id in removeHandlerMap" );
        }
        m_removeHandlerSet.insert( torrentHandle.id() );
        m_removeHandler = removeHandler;
        m_session.remove_torrent( torrentHandle, lt::session::delete_partfile );
    }

    // addTorrentFileToSession
    virtual lt_handle addTorrentFileToSession( std::string torrentFilename,
                                          std::string savePath,
                                          endpoint_list list ) override {

        // read torrent file
        std::ifstream torrentFile( torrentFilename );
        std::vector<char> buffer( (std::istreambuf_iterator<char>(torrentFile)), std::istreambuf_iterator<char>() );

        // create add_torrent_params
        lt::add_torrent_params params;
        params.flags &= ~lt::torrent_flags::paused;
        params.flags &= ~lt::torrent_flags::auto_managed;
        params.storage_mode = lt::storage_mode_sparse;
        params.save_path = fs::path(savePath);
        params.ti = std::make_shared<lt::torrent_info>( buffer, lt::from_span );

        //dbg///////////////////////////////////////////////////
        auto tInfo = lt::torrent_info(buffer, lt::from_span);
//        LOG( tInfo.info_hashes().v2.to_string() ) );
//        LOG( tInfo.info_hashes().v2 ) );
//        LOG( "add torrent: torrent filename:" << torrentFilename );
//        LOG( "add torrent: fileFolder:" << fileFolder );
        //LOG( "add torrent: " << lt::make_magnet_uri(tInfo) );
        //dbg///////////////////////////////////////////////////

        lt::torrent_handle tHandle = m_session.add_torrent(params);

        connectPeers( tHandle, list );

        return tHandle;
    }

    // addActionListToSession
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
//        fs::create_directories( addFilesFolder );
        {
            std::ofstream fileStream( fs::path(sandboxFolder)/"stub", std::ios::binary );
//            //fileStream.write("12345",5);
        }

        // parse action list
        for( auto& action : actionList ) {

            switch ( action.m_actionId ) {
                case action_list_id::upload: {
                    fs::path sandboxFilePath = addFilesFolder/action.m_param2;
                    if ( !isPathInsideFolder( sandboxFolder, addFilesFolder ) )
                    {
                        LOG( action.m_param2 );
                        LOG( sandboxFilePath );
                        LOG( addFilesFolder );
                        throw std::runtime_error( "invalid destination path in 'upload' action: " + action.m_param2 );
                    }

                    LOG( action.m_param1 );
                    LOG( action.m_param2 );
                    LOG( addFilesFolder );
                    LOG( sandboxFilePath );

                    fs::create_directories( sandboxFilePath.parent_path() );
                    std::string target = action.m_param1;
                    LOG( target );
//                    fs::create_symlink( target, sandboxFilePath);
					fs::copy( target, sandboxFilePath );
                    break;
                }
                default:
                    break;
            }
        }

        // save ActionList
        actionList.serialize( fs::path(sandboxFolder)/"ActionList.bin" );

        // create torrent file
        InfoHash infoHash = createTorrentFile( fs::path(sandboxFolder), workFolder, fs::path(sandboxFolder)/"root.torrent" );

        // add torrent file
        addTorrentFileToSession( fs::path(sandboxFolder)/"root.torrent", workFolder, list );

        return infoHash;
    }

    // downloadFile
    virtual void downloadFile( InfoHash infoHash,
                               std::string outputFolder,
                               DownloadHandler downloadHandler,
                               endpoint_list list ) override {

        //LOG( "downloadFile: " << toString(infoHash) );

        // create add_torrent_params
        lt::add_torrent_params params = lt::parse_magnet_uri( magnetLink(infoHash) );

        // where the file will be placed
        params.save_path = outputFolder;

        // create torrent_handle
        lt::torrent_handle tHandle = m_session.add_torrent(params);

        if ( !m_session.is_valid() )
            throw std::runtime_error("downloadFile: libtorrent session is not valid");

        if ( !tHandle.is_valid() )
            throw std::runtime_error("downloadFile: torrent handle is not valid");

        // connect to peers
        for( auto endpoint : list ) {
            tHandle.connect_peer(endpoint);
        }

        // save download handler
        m_downloadHandlerMap[tHandle.id()] = downloadHandler;
    }

private:

    void alertHandler() {

        // extract alerts
        std::vector<lt::alert *> alerts;
        m_session.pop_alerts(&alerts);

        // loop by alerts
        for (auto &alert : alerts) {

            switch (alert->type()) {

                case lt::torrent_deleted_alert::alert_type: {
                    auto *alertInfo = dynamic_cast<lt::torrent_deleted_alert*>(alert);
                    //LOG( "*** lt::torrent_deleted_alert:" << alertInfo->handle.info_hashes().v2 );
                    LOG( "*** lt::torrent_deleted_alert:" << alertInfo->handle.torrent_file()->files().file_name(0).to_string() );
                    LOG( "*** lt::torrent_deleted_alert:" << alertInfo->handle.torrent_file()->files().file_path(0) );
                    //LOG( "*** get_torrents().size()=" << m_session.get_torrents().size() );
                    
                    if ( auto it = m_removeHandlerSet.find(alertInfo->handle.id()); it != m_removeHandlerSet.end() )
                    {
                        m_removeHandlerSet.erase( it );
                        if ( m_removeHandlerSet.size() == 0 )
                        {
                            if ( m_removeHandler.has_value() )
                                m_removeHandler.value()();
                            return;
                        }
                    }

                    auto it = m_downloadHandlerMap.find(alertInfo->handle.id());

                    if ( it != m_downloadHandlerMap.end() )
                    {
                        DownloadHandler handler = it->second;

                        // remove entry from downloadHandlerMap
                        m_downloadHandlerMap.erase( it );

                        auto ltHash = alertInfo->handle.info_hashes().v2;
                        InfoHash hash;
                        if ( ((size_t)ltHash.size()) != hash.size() )
                        {
                            LOG("ltHash.size() != hash.size()");
                            handler( download_status::failed, InfoHash(), "internal error 'ltHash.size() != hash.size()'" );
                        }
                        else
                        {
                            memcpy( hash.data(), ltHash.data(), hash.size() );
                            handler( download_status::complete, hash, "" );
                        }
                        
                        // remove entry from downloadHandlerMap
                        //m_downloadHandlerMap.erase( it );
                    }

                    break;
                }

                // piece_finished_alert
                case lt::piece_finished_alert::alert_type: {

                    auto *alertInfo = dynamic_cast<lt::piece_finished_alert *>(alert);

                    if ( alertInfo ) {

                        // TODO: better to use piece_granularity
                        std::vector<int64_t> fp = alertInfo->handle.file_progress();

                        // check completeness
                        bool isAllComplete = true;
                        for( uint32_t i=0; i<fp.size(); i++ ) {

                            auto fsize = alertInfo->handle.torrent_file()->files().file_size(i);
                            bool const complete = ( fp[i] == fsize );

                            isAllComplete = isAllComplete && complete;

                            //dbg/////////////////////////
//                            const std::string fileName = alertInfo->handle.torrent_file()->files().file_name(i).to_string();
//                            const std::string filePath = alertInfo->handle.torrent_file()->files().file_path(i);
//                            LOG( m_addressAndPort << ": " << filePath
//                                      << ": alert: progress: " << fp[i] << " of " << fsize );
                            //dbg/////////////////////////
                        }
//                        LOG( "-" );

                        if ( isAllComplete ) {

                            auto it = m_downloadHandlerMap.find(alertInfo->handle.id());

                            if ( it != m_downloadHandlerMap.end() ) {
                                //LOG( "*** native_handle:" << alertInfo->handle.native_handle() );

                                // get peers info
                                std::vector<lt::peer_info> peers;
                                alertInfo->handle.get_peer_info(peers);

                                for (const lt::peer_info &pi : peers) {
                                    LOG("Peer ip: " << pi.ip.address().to_string())
                                    LOG("Peer id: " << pi.pid.to_string())

                                    // the total number of bytes downloaded from and uploaded to this peer.
                                    // These numbers do not include the protocol chatter, but only the
                                    // payload data.
                                    LOG("Total download: " << pi.total_download)
                                    LOG("Total upload: " << pi.total_upload)
                                }

                                // remove torrent
                                m_session.remove_torrent( alertInfo->handle, lt::session::delete_partfile );
                            }
                        }

                    }
                    break;
                }

                case lt::listen_failed_alert::alert_type: {
                    this->m_alertHandler( alert );

                    auto *alertInfo = dynamic_cast<lt::listen_failed_alert *>(alert);

                    if ( alertInfo ) {
                        LOG(  "listen error: " << alertInfo->message())
                    }
                    break;
                }

                case lt::portmap_error_alert::alert_type: {
                    auto *alertInfo = dynamic_cast<lt::portmap_error_alert *>(alert);

                    if ( alertInfo ) {
                        LOG(  "portmap error: " << alertInfo->message())
                    }
                    break;
                }

                case lt::dht_error_alert::alert_type: {
                    auto *alertInfo = dynamic_cast<lt::dht_error_alert *>(alert);

                    if ( alertInfo ) {
                        LOG(  "dht error: " << alertInfo->message())
                    }
                    break;
                }

                case lt::session_error_alert::alert_type: {
                    auto *alertInfo = dynamic_cast<lt::session_error_alert *>(alert);

                    if ( alertInfo ) {
                        LOG(  "session error: " << alertInfo->message())
                    }
                    break;
                }

                case lt::udp_error_alert::alert_type: {
                    auto *alertInfo = dynamic_cast<lt::udp_error_alert *>(alert);

                    if ( alertInfo ) {
                        LOG(  "udp error: " << alertInfo->message())
                    }
                    break;
                }

                case lt::torrent_error_alert::alert_type: {
                    auto *alertInfo = dynamic_cast<lt::torrent_error_alert *>(alert);

                    if ( alertInfo ) {
                        LOG(  m_addressAndPort << ": torrent error: " << alertInfo->message())
                    }
                    break;
                }

                case lt::peer_error_alert::alert_type: {
                    auto *alertInfo = dynamic_cast<lt::peer_error_alert *>(alert);

                    if ( alertInfo ) {
                        LOG(  "peer error: " << alertInfo->message())
                    }
                    break;
                }

                case lt::peer_disconnected_alert::alert_type: {
                    auto *alertInfo = dynamic_cast<lt::peer_disconnected_alert *>(alert);

                    if ( alertInfo ) {
                        //LOG(  m_addressAndPort << ": peer disconnected: " << alertInfo->message())
                    }
                    break;
                }

                case lt::file_error_alert::alert_type: {
                    auto *alertInfo = dynamic_cast<lt::file_error_alert *>(alert);

                    if ( alertInfo ) {
                        LOG(  "file error: " << alertInfo->message())
                    }
                    break;
                }

                case lt::block_uploaded_alert::alert_type: {
                    auto *alertInfo = dynamic_cast<lt::block_uploaded_alert *>(alert);

                    if (alertInfo) {
//                        LOG("block_uploaded: " << alertInfo->message())
//
//                        // get peers info
//                        std::vector<lt::peer_info> peers;
//                        alertInfo->handle.get_peer_info(peers);
//
//                        for (const lt::peer_info &pi : peers) {
//                            LOG("Upload. Peer ip: " << pi.ip.address().to_string())
//                            LOG("Upload. Peer id: " << pi.pid.to_string())

                            // the total number of bytes downloaded from and uploaded to this peer.
                            // These numbers do not include the protocol chatter, but only the
                            // payload data.
//                            LOG("Upload. Total download: " << pi.total_download)
//                            LOG("Upload. Total upload: " << pi.total_upload)
//                        }
                    }
                    break;
                }

                case lt::log_alert::alert_type: {
                    auto *alertInfo = dynamic_cast<lt::file_error_alert *>(alert);

                    if ( alertInfo ) {
                        LOG(  "log_alert: " << alertInfo->message())
                    }
                    break;
                }

                default: {
                    //LOG( "other alert: " << alert->message() );
                }
            }
        }
    }
};

//
// ('static') createTorrentFile
//
InfoHash createTorrentFile( std::string fileOrFolder, std::string /*rootFolder*/, std::string outputTorrentFilename )
{
    // setup file storage
    lt::file_storage fStorage;
    lt::add_files( fStorage, fileOrFolder, lt::create_flags_t{} );

    // create torrent info
    lt::create_torrent createInfo( fStorage, 16*1024, lt::create_torrent::v2_only );

    // calculate hashes for 'fileOrFolder' relative to 'rootFolder'
//    lt::error_code ec;
//    lt::set_piece_hashes( createInfo, rootFolder, ec );
//    std::cout << ec.category().name() << ':' << ec.value();
    lt::set_piece_hashes( createInfo, fs::path(fileOrFolder).parent_path() );

    // generate metadata
    lt::entry entry_info = createInfo.generate();

    // convert to bencoding
    std::vector<char> torrentFileBytes;
//    entry_info["info"].dict()["xpx"]=lt::entry("pub_key/folder1");
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

// createDefaultLibTorrentSession
std::shared_ptr<Session> createDefaultSession( std::string address,
                                               const LibTorrentErrorHandler& alertHandler )
{
    return std::make_shared<DefaultSession>( address, alertHandler );
}

}}

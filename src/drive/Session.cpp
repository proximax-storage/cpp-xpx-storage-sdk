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
#include <libtorrent/torrent.hpp>
#include <libtorrent/extensions/ut_metadata.hpp>

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
class DefaultSession: public Session {

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

        // todo public_key?
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
        params.storage_mode = lt::storage_mode_sparse;
        params.save_path = fs::path(savePath);
        params.ti = std::make_shared<lt::torrent_info>( buffer, lt::from_span );

        //dbg///////////////////////////////////////////////////
//        auto tInfo = lt::torrent_info(buffer, lt::from_span);
//        LOG( tInfo.info_hashes().v2.to_string() ) );
//        LOG( tInfo.info_hashes().v2 ) );
//        LOG( "add torrent: torrent filename:" << torrentFilename );
//        LOG( "add torrent: fileFolder:" << fileFolder );
        //LOG( "add torrent: " << lt::make_magnet_uri(tInfo) );
        //dbg///////////////////////////////////////////////////

        lt::torrent_handle tHandle = m_session.add_torrent(params);

        //TODO!!!
        //todo++
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

                    fs::create_directories( sandboxFilePath.parent_path() );
                    fs::create_symlink( action.m_param1, sandboxFilePath);
                    //fs::copy( action.m_param1, addFilesFolder/action.m_param2 );
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
            if ( downloadContext.m_downloadType == DownloadContext::fs_tree )
                downloadContext.m_saveAs = fs::path(tmpFolder) / "FsTree.bin";

            m_downloadMap[ tHandle.id() ] = DownloadMapCell{ tmpFolder, {std::move(downloadContext)} };

        }
    }

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

private:

    void alertHandler() {

        // extract alerts
        std::vector<lt::alert *> alerts;
        m_session.pop_alerts(&alerts);

        // loop by alerts
        for (auto &alert : alerts) {

//            if ( alert->type() != lt::log_alert::alert_type )
//            {
//                LOG( "type:" << alert->type() << ":  " << alert->message() );
//            }

            switch (alert->type()) {
                case lt::peer_log_alert::alert_type: {
                    if ( auto *theAlert = dynamic_cast<lt::peer_log_alert *>(alert); theAlert->direction == lt::peer_log_alert::incoming_message )
                    {
                        //LOG( "#: " << m_addressAndPort << ": peer_log_alert: " << alert->message() << "\n" );
                    }
                    break;
                }

//                case lt::peer_alert::alert_type: {
//                    LOG(  m_addressAndPort << ": peer_alert: " << alert->message())
//                    break;
//                }

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
//                            const std::string filePath = theAlert->handle.torrent_file()->files().file_path(i);
//                            LOG( m_addressAndPort << ": " << filePath << ": alert: progress: " << fp[i] << " of " << fsize );
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

//                                for (const lt::peer_info &pi : peers)
//                                {
//                                    LOG("Peer ip: " << pi.ip.address().to_string())
//                                    LOG("Peer id: " << pi.pid.to_string())
//
//                                    // the total number of bytes downloaded from and uploaded to this peer.
//                                    // These numbers do not include the protocol chatter, but only the
//                                    // payload data.
//                                    LOG("Total download: " << pi.total_download)
//                                    LOG("Total upload: " << pi.total_upload)
//                                }

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

//                case lt::incoming_connection_alert::alert_type: {
//                    LOG("!!!!! incoming_connection_alert");
//                    break;
//                }
//                case lt::incoming_request_alert::alert_type: {
//                    LOG("!!!!! incoming_request_alert");
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
                        LOG(  "peer error: " << theAlert->message())
                    }
                    break;
                }

                case lt::peer_disconnected_alert::alert_type: {
                    auto *theAlert = dynamic_cast<lt::peer_disconnected_alert *>(alert);

                    if ( theAlert ) {
                        //LOG(  m_addressAndPort << ": peer disconnected: " << theAlert->message())
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
//TODO download statistic
//                    if (theAlert) {
//                        LOG("block_uploaded: " << theAlert->message())
//
//                        // get peers info
//                        std::vector<lt::peer_info> peers;
//                        theAlert->handle.get_peer_info(peers);
//
//                        for (const lt::peer_info &pi : peers) {
//                            LOG("Upload. Peer ip: " << pi.ip.address().to_string())
//                            LOG("Upload. Peer id: " << pi.pid.to_string())
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

    //LOG( "file infoHash :" << toString(infoHash) );
    //LOG( "infoHash2:" << toString(infoHash2) );
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
std::shared_ptr<Session> createDefaultSession( std::string address,
                                               const LibTorrentErrorHandler& alertHandler )
{
    return std::make_shared<DefaultSession>( address, alertHandler );
}

}}

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
    using DownloadMap      = std::map<std::uint32_t,DownloadContext>;

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

        // save ActionList
        actionList.serialize( fs::path(sandboxFolder)/"actionList.bin" );

        // create torrent file
        InfoHash infoHash = createTorrentFile( fs::path(sandboxFolder), workFolder, fs::path(sandboxFolder)/"root.torrent" );

        // add torrent file
        addTorrentFileToSession( fs::path(sandboxFolder)/"root.torrent", workFolder, list );

        return infoHash;
    }

    // downloadFile
    virtual void downloadFile( const DownloadContext& downloadContext,
                               endpoint_list          list  ) override {

        // create add_torrent_params
        lt::error_code ec;
        lt::add_torrent_params params = lt::parse_magnet_uri( magnetLink(downloadContext.m_infoHash), ec );
        if (ec) {
            throw std::runtime_error( std::string("downloadFile error: ") + ec.message() );
        }

        // where the file will be placed
        params.save_path = downloadContext.m_saveFolder;

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
        std::lock_guard locker(m_downloadMapMutex);
        m_downloadMap.emplace( std::pair<std::uint32_t,DownloadContext>{ tHandle.id(), downloadContext });
    }

    void connectPeers( lt::torrent_handle tHandle, endpoint_list list ) {

        if ( !m_session.is_valid() )
            throw std::runtime_error("connectPeers: libtorrent session is not valid");

        //TODO check if not set m_lastTorrentFileHandle
        for( auto endpoint : list ) {
            tHandle.connect_peer(endpoint);
        }
    }

private:

    void alertHandler() {

        // extract alerts
        std::vector<lt::alert *> alerts;
        m_session.pop_alerts(&alerts);

        // loop by alerts
        for (auto &alert : alerts) {

            switch (alert->type()) {
//                case lt::peer_log_alert::alert_type: {
//                    LOG( m_addressAndPort << ":peer_log_alert: " << alert->message() );
//                    break;
//                }

                case lt::torrent_deleted_alert::alert_type: {
                    auto *alertInfo = dynamic_cast<lt::torrent_deleted_alert*>(alert);
                    //LOG( "*** lt::torrent_deleted_alert:" << alertInfo->handle.info_hashes().v2 );
                    LOG( "*** lt::torrent_deleted_alert:" << alertInfo->handle.torrent_file()->files().file_name(0) );
                    LOG( "*** lt::torrent_deleted_alert:" << alertInfo->handle.torrent_file()->files().file_path(0) );
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
                            if ( auto torrentIt  = removeContext.m_torrentSet.find(alertInfo->handle);
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
                    if ( auto downloadConextIt  = m_downloadMap.find(alertInfo->handle.id());
                              downloadConextIt != m_downloadMap.end() )
                    {
                        // do notification
                        DownloadContext context = downloadConextIt->second;
                        context.m_downloadNotification( context, download_status::complete, "" );

                        // remove entry from downloadHandlerMap
                        std::lock_guard<std::mutex> locker(m_downloadMapMutex);
                        m_downloadMap.erase( downloadConextIt );
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
//                            LOG( alertInfo->handle.info_hash() );
//                            LOG( m_addressAndPort << ": " << filePath
//                                      << ": alert: progress: " << fp[i] << " of " << fsize );
                            //dbg/////////////////////////
                        }
//                        LOG( "-" );

                        if ( isAllComplete )
                        {
                            auto it = m_downloadMap.find(alertInfo->handle.id());

                            if ( it != m_downloadMap.end() )
                            {
                                // get peers info
                                std::vector<lt::peer_info> peers;
                                alertInfo->handle.get_peer_info(peers);

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
//                    auto *alertInfo = dynamic_cast<lt::block_uploaded_alert *>(alert);
//TODO download statistic
//                    if (alertInfo) {
//                        LOG("block_uploaded: " << alertInfo->message())
//
//                        // get peers info
//                        std::vector<lt::peer_info> peers;
//                        alertInfo->handle.get_peer_info(peers);
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
                    auto *alertInfo = dynamic_cast<lt::file_error_alert *>(alert);

                    if ( alertInfo ) {
                        LOG(  "log_alert: " << alertInfo->message())
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
                                      const std::string& outputTorrentPath )
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
    LOG( "entry_info[info]:" << entry_info["info"].to_string() );
    //auto tInfo = lt::torrent_info(torrentFileBytes, lt::from_span);
    //LOG( "make_magnet_uri:" << lt::make_magnet_uri(tInfo) );
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

    LOG( "entry[info]:" << entry_info["info"].to_string() );

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

    LOG( "infoHash :" << toString(infoHash) );
    LOG( "infoHash2:" << toString(infoHash2) );
    assert( infoHash == infoHash2 );

    // write to file
    if ( !outputTorrentPath.empty() )
    {
        std::ofstream fileStream( fs::path(outputTorrentPath) / (newFileName +".torrent"), std::ios::binary );
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

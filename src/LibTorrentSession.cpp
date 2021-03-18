/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "LibTorrentSession.h"
#include "utils.h"

#include <iostream>
#include <vector>
#include <filesystem>
#include <fstream>

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

namespace xpx_storage_sdk {

class DefaultLibTorrentWrapper: public LibTorrentSession {

    std::string m_addressAndPort;
    lt::session m_session;

    std::map<lt::torrent_handle,std::pair<DownloadHandler,InfoHash>> m_downloadHandlerMap;

    std::string m_dbgLabel;

public:

    DefaultLibTorrentWrapper( std::string address ) : m_addressAndPort(address), m_dbgLabel(address) {
        createSession();
    }

    virtual ~DefaultLibTorrentWrapper() {}

    // createSession
    void createSession() {

        lt::settings_pack settingsPack;

        settingsPack.set_int( lt::settings_pack::alert_mask, lt::alert_category::all );
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

    // addTorrentFileToSession
    virtual void addTorrentFileToSession( std::string torrentFilename,
                                          std::string fileFolder,
                                          endpoint_list = endpoint_list() ) override {

        // read torrent file
        std::ifstream torrentFile( torrentFilename );
        std::vector<char> buffer( (std::istreambuf_iterator<char>(torrentFile)), std::istreambuf_iterator<char>() );

        // create add_torrent_params
        lt::add_torrent_params params;
        params.flags &= ~lt::torrent_flags::paused;
        params.flags &= ~lt::torrent_flags::auto_managed;
        params.storage_mode = lt::storage_mode_sparse;
        params.save_path = fs::path(fileFolder).parent_path().string();
        params.ti = std::make_shared<lt::torrent_info>( buffer, lt::from_span );

        //dbg///////////////////////////////////////////////////
        auto tInfo = lt::torrent_info(buffer, lt::from_span);
//        std::cout << tInfo.info_hashes().v2.to_string() << std::endl;
//        std::cout << tInfo.info_hashes().v2 << std::endl;
        std::cout << "add torrent: torrent filename:" << torrentFilename << std::endl;
        std::cout << "add torrent: fileFolder:" << fileFolder << std::endl;
        std::cout << "add torrent: " << lt::make_magnet_uri(tInfo) << std::endl;
        //dbg///////////////////////////////////////////////////

        m_session.add_torrent(params);
    }

    // addActionListToSession
    InfoHash addActionListToSession( const ActionList& actionList,
                                     const std::string& tmpFolderPath,
                                     endpoint_list list = endpoint_list() ) override {
        // clear tmpFolder
        std::error_code ec;
        fs::remove_all( tmpFolderPath, ec );
        fs::create_directory( tmpFolderPath );

        // path to root folder
        fs::path addFilesFolder = fs::path(tmpFolderPath).append( "root" );

        // parse action list
        for( auto& action : actionList ) {

            switch ( action.m_actionId ) {
                case action_list_id::upload: {
                    fs::create_symlink( action.m_param1, addFilesFolder.string()+action.m_param2 );
                    break;
                }
                default:
                    break;
            }
        }

        // save ActionList
        actionList.serialize( fs::path(tmpFolderPath)/"actionList.bin" );

        // create torrent file
        InfoHash infoHash = createTorrentFile( fs::path(tmpFolderPath), fs::path(tmpFolderPath)/"root.torrent" );

        // add torrent file
        addTorrentFileToSession( fs::path(tmpFolderPath)/"root.torrent", fs::path(tmpFolderPath) );

        return infoHash;
    }

    // downloadFile
    virtual void downloadFile( InfoHash infoHash,
                               std::string outputFolder,
                               DownloadHandler downloadHandler,
                               endpoint_list list = endpoint_list() ) override {

        LOG( "downloadFile: " << toString(infoHash) );

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
        m_downloadHandlerMap[tHandle] = std::pair<DownloadHandler,InfoHash>(downloadHandler,infoHash);
    }

private:

    void alertHandler() {

        // extract alerts
        std::vector<lt::alert *> alerts;
        m_session.pop_alerts(&alerts);

        // loop by alerts
        for (auto &alert : alerts) {
            //LOG( m_dbgLabel << "(alert): " << alert->message() );

            switch (alert->type()) {

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
                            const std::string fileName = alertInfo->handle.torrent_file()->files().file_name(i).to_string();
                            const std::string filePath = alertInfo->handle.torrent_file()->files().file_path(i);

                            std::cout << m_addressAndPort << ": " << filePath
                                      << ": alert: progress: " << fp[i] << " of " << fsize << std::endl;
                            
                            //dbg/////////////////////////
                        }
//                        std::cout << "-" << std::endl;

                        // notify about the end of the download
                        if ( isAllComplete ) {
                            
                            auto it = m_downloadHandlerMap.find(alertInfo->handle);
                            
                            if ( it != m_downloadHandlerMap.end() ) {
                                DownloadHandler handler = it->second.first;
                                InfoHash hash = it->second.second;
                                handler( download_status::complete, hash, "" );
                                m_downloadHandlerMap.erase( it );
                            }
                        }

                    }
                    break;
                }
                default: {
                    //std::cout << "other alert: " << alert->message() << std::endl;
                }
            }
        }
    }
};

//
// ('static') createTorrentFile
//
InfoHash createTorrentFile( std::string pathToFolderOrFolder, std::string outputTorrentFilename )
{
    // setup file storage
    lt::file_storage fStorage;
    lt::add_files( fStorage, fs::path(pathToFolderOrFolder).string(), lt::create_flags_t{} );

    // create torrent info
    lt::create_torrent createInfo( fStorage, 16*1024, lt::create_torrent::v2_only );

    // calculate hashes
    lt::set_piece_hashes( createInfo, fs::path(pathToFolderOrFolder).parent_path().string() );

    // generate metadata
    lt::entry entry_info = createInfo.generate();

    // convert to bencoding
    std::vector<char> torrentFileBytes;
    lt::bencode(std::back_inserter(torrentFileBytes), entry_info); // metainfo -> binary

    //dbg////////////////////////////////
    auto entry = entry_info;
    std::cout << entry["info"].to_string() << std::endl;

    auto tInfo = lt::torrent_info(torrentFileBytes, lt::from_span);
    std::cout << tInfo.info_hashes().v2 << std::endl;
    
    std::cout << lt::make_magnet_uri(tInfo) << std::endl;
    //std::cout << entry.to_string() << std::endl;
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
std::shared_ptr<LibTorrentSession> createDefaultLibTorrentSession( std::string address ){
    return std::make_shared<DefaultLibTorrentWrapper>( address );
}

};

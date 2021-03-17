/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "LibTorrentWrapper.h"
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

//#include<libtorrent/bencode.hpp>
//#include<libtorrent/torrent_info.hpp>
//#include<libtorrent/sha256.hpp>

// boost
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>


namespace fs = std::filesystem;

namespace xpx_storage_sdk {

class DefaultLibTorrentWrapper: public LibTorrentWrapper {

    std::string m_addressAndPort;
    lt::session m_session;

    std::map<lt::torrent_handle,std::pair<DownloadHandler,InfoHash>> m_downloadHandlerMap;

    std::string m_dbgLabel;

public:

    DefaultLibTorrentWrapper( std::string address ) : m_addressAndPort(address), m_dbgLabel(address) {}

    virtual ~DefaultLibTorrentWrapper() {}

    // createSession
    void createSession() override {

        lt::settings_pack settingsPack;

        settingsPack.set_int( lt::settings_pack::alert_mask, lt::alert_category::all );
        settingsPack.set_str( lt::settings_pack::dht_bootstrap_nodes, "" );

        boost::uuids::uuid uuid = boost::uuids::random_generator()();

        settingsPack.set_str(  lt::settings_pack::user_agent, boost::uuids::to_string(uuid) );
        settingsPack.set_bool( lt::settings_pack::enable_dht, true );
        settingsPack.set_bool( lt::settings_pack::enable_lsd, true );
        settingsPack.set_str(  lt::settings_pack::listen_interfaces, m_addressAndPort );
        settingsPack.set_bool( lt::settings_pack::allow_multiple_connections_per_ip, true );

        m_session.apply_settings(settingsPack);
        m_session.set_alert_notify( [this] { alertHandler(); } );
    }

    virtual void deleteSession() override {
        m_downloadHandlerMap.clear();
        //TODO abort?
        m_session.abort();
    }

    // addTorrentFileToSession
    virtual bool addTorrentFileToSession( std::string torrentFilename,
                                          std::string fileFolder,
                                          endpoint_list = endpoint_list() ) override {

        // read torrent file
        std::ifstream torrentFile( torrentFilename );
        std::vector<char> buf( (std::istreambuf_iterator<char>(torrentFile)), std::istreambuf_iterator<char>() );

        // create add_torrent_params
        lt::add_torrent_params tp;
        tp.flags &= ~lt::torrent_flags::paused;
        tp.flags &= ~lt::torrent_flags::auto_managed;
        tp.storage_mode = lt::storage_mode_sparse;
        tp.save_path = fs::path(fileFolder).parent_path().string();
        tp.ti = std::make_shared<lt::torrent_info>( buf, lt::from_span );

        //dbg///////////////////////////////////////////////////
        auto tInfo = lt::torrent_info(buf, lt::from_span);
//        std::cout << tInfo.info_hashes().v2.to_string() << std::endl;
//        std::cout << tInfo.info_hashes().v2 << std::endl;
        std::cout << "add torrentFilename:" << lt::make_magnet_uri(tInfo) << std::endl;
        //dbg///////////////////////////////////////////////////

        lt::error_code ec;
        m_session.add_torrent(tp,ec);
        if (ec.value() != 0) {
            //handler(error::failed, ec.message());
            return false;
        }

        return true;
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
        lt::error_code ec;
        lt::add_torrent_params tp = lt::parse_magnet_uri( magnetLink(infoHash), ec);
        if (ec.value() != 0) {
            //handler(download_status::failed, hash, "");
            return;
        }

        tp.save_path = outputFolder;

        // create torrent_handle
        lt::torrent_handle th = m_session.add_torrent(tp, ec);
        if (ec.value() != 0) {
            //TODO exception
            return;
        }

        // connect to peers
        if (m_session.is_valid() && th.is_valid()) {

            for( auto endpoint : list ) {
                //LOG( endpoint );
                th.connect_peer(endpoint);
            }
        }
        else {
            //TODO exception
        }

        m_downloadHandlerMap[th] = std::pair<DownloadHandler,InfoHash>(downloadHandler,infoHash);
    }

private:

    void alertHandler() {

        std::vector<lt::alert *> alerts;
        m_session.pop_alerts(&alerts);

        for (auto &alert : alerts) {
            //LOG( m_dbgLabel << "(alert): " << alert->message() );

            switch (alert->type()) {

                // piece_finished_alert
                case lt::piece_finished_alert::alert_type: {

                    auto *pa = dynamic_cast<lt::piece_finished_alert *>(alert);

                    if (pa) {

                        // TODO: better to use piece_granularity
                        std::vector<int64_t> fp = pa->handle.file_progress();

                        bool isAllComplete = true;
                        for( uint32_t i=0; i<fp.size(); i++ ) {
                            
                            const std::string fileName = pa->handle.torrent_file()->files().file_name(i).to_string();
                            const std::string filePath = pa->handle.torrent_file()->files().file_path(i);

                            bool const complete = ( fp[i] == pa->handle.torrent_file()->files().file_size(i) );

                            isAllComplete = isAllComplete && complete;
                            
                            if ( complete ) {
                                std::cout << m_addressAndPort << ": total? downloaded: " << fp[i] << std::endl;
                                std::cout << m_addressAndPort << ": fname: " << filePath << std::endl;
                            }

                            std::cout << m_addressAndPort << ": " << filePath << ": alert: progress: " << fp[i] << std::endl;
                            
                        }
                        std::cout << "-" << std::endl;

                        if ( isAllComplete ) {
                            
                            auto it = m_downloadHandlerMap.find(pa->handle);
                            
                            if ( it != m_downloadHandlerMap.end() ) {
                                DownloadHandler handler = it->second.first;
                                InfoHash hash = it->second.second;
                                handler( download_status::complete, hash, "" );
                            }
                        }

                    }
                    break;
                }
                default: {
                    //std::cout << "alert: " << alert->message() << std::endl;
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
    lt::file_storage fs;
    lt::add_files( fs, fs::path(pathToFolderOrFolder).string(), lt::create_flags_t{} );

    // create torrent creator
    lt::create_torrent torrent( fs, 16*1024, lt::create_torrent::v2_only );

    // calculate hashes
    lt::set_piece_hashes( torrent, fs::path(pathToFolderOrFolder).parent_path().string() );

    // generate metadata
    lt::entry entry_info = torrent.generate();

    // convert to bencoding
    std::vector<char> torrentfile_source;
    lt::bencode(std::back_inserter(torrentfile_source), entry_info); // metainfo -> binary

    //dbg////////////////////////////////
    auto entry = entry_info;
    std::cout << entry["info"].to_string() << std::endl;

    auto tInfo = lt::torrent_info(torrentfile_source, lt::from_span);
    std::cout << tInfo.info_hashes().v2 << std::endl;
    
    std::cout << lt::make_magnet_uri(tInfo) << std::endl;
    //std::cout << entry.to_string() << std::endl;
    //dbg////////////////////////////////

    // get infoHash
    lt::torrent_info torrentInfo( torrentfile_source, lt::from_span );
    auto hashBytes = torrentInfo.info_hashes().v2.to_string();
    InfoHash infoHash;
    if ( hashBytes.size()==32 ) {
        memcpy( &infoHash[0], &hashBytes[0], 32 );
    }

    // write to file
    if ( !outputTorrentFilename.empty() )
    {
        std::ofstream fileStream( outputTorrentFilename, std::ios::binary );
        fileStream.write(torrentfile_source.data(),torrentfile_source.size());
    }

    return infoHash;
}

// createDefaultLibTorrentWrapper
std::shared_ptr<LibTorrentWrapper> createDefaultLibTorrentWrapper( std::string address ){
    return std::make_shared<DefaultLibTorrentWrapper>( address );
}

};

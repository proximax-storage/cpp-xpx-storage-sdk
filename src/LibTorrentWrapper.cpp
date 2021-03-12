/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "LibTorrentWrapper.h"

#include<iostream>
#include<vector>
#include<unistd.h>

#include<libtorrent/bencode.hpp>
#include<libtorrent/torrent_info.hpp>
#include<libtorrent/create_torrent.hpp>
#include<libtorrent/sha256.hpp>

#include <filesystem>

namespace fs = std::filesystem;

namespace xpx_storage_sdk {

    void LibTorrentWrapper::createSession( const std::string& address ) {
    }

    void LibTorrentWrapper::deleteSession() {
    }

    bool LibTorrentWrapper::addTorrentFileToSession( std::string torrentFilename, std::string peerAddrWithPort ) {

    }

    void LibTorrentWrapper::downloadFile( std::string fileHash, std::string outputFolder, DownloadHandler, std::string peerAddrWithPort ) {

    };

    InfoHash LibTorrentWrapper::createTorrentFile( std::string pathToFolderOrFolder, std::string outputTorrentFilename )
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

};

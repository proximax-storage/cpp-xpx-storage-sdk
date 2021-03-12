/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "PrepareActionListToUpload.h"
#include "LibTorrentWrapper.h"
#include <filesystem>

namespace fs = std::filesystem;

#include <libtorrent/create_torrent.hpp>
#include <filesystem>

namespace fs = std::filesystem;


namespace xpx_storage_sdk {

FileHash createRootHash( std::string pathToFileOrFolder ) {

    lt::file_storage fileStorage;
    lt::add_files( fileStorage, pathToFileOrFolder );

    const int piece_size = 16; //todoas parameter
    lt::create_torrent cTorrent ( fileStorage, piece_size, lt::create_torrent::v2_only);
    lt::set_piece_hashes( cTorrent, fs::path(pathToFileOrFolder).parent_path().string() );

    std::vector<char> buf;
    lt::entry entry_info = cTorrent.generate();

    auto fileTree = entry_info["info"]["file tree"];
    //TODO
    auto hash = entry_info["info"]["file tree"][0][0]["pieces root"];

    return FileHash();
}



    FileHash prepareActionListToUpload( const ActionList& actionList, const std::string& tmpFolderPath ) {

        std::error_code ec;
        fs::remove_all( tmpFolderPath, ec );
        fs::create_directory( tmpFolderPath );

        fs::path blobFolder(tmpFolderPath);
        blobFolder.append( "blob" );
        for( auto& action : actionList ) {

            switch ( action.m_actionId ) {
                case action_list_id::upload: {
                    fs::create_symlink( action.m_param1, blobFolder.string()+action.m_param1 );
                    break;
                }
                default:
                    break;
            }
        }

//        FileHash blobHash = LibTorrentWrapper::createTorrentFile( blobFolder.string(), fs::path(tmpFolderPath)/"root.torrent" );

//        ActionList actionList2 = actionList;
//        actionList2.push_back( Action{action_list_id::blob_hash,blobHash} );

//        actionList2.serialize( fs::path(tmpFolderPath)/"actionList.bin" );

//        FileHash hash = LibTorrentWrapper::createTorrentFile( fs::path(tmpFolderPath)/"actionList.bin", fs::path(tmpFolderPath)/"actionList.torrent" );

        return FileHash();
    }
}

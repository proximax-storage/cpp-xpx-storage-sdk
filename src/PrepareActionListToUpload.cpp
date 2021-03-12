/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "PrepareActionListToUpload.h"
#include "LibTorrentWrapper.h"
#include <filesystem>

namespace fs = std::filesystem;

//{ 'creation date': 1615509047,
//  'info': {
//        'file tree': {
//'bc.log': { '': { 'length': 341631592, 'pieces root': '1da5ee9b28e8fe3a02426621a52a4f59874e7d062fb922d5466cf544ab3d847d' } },
//'xxx.txt': { '': { 'length': 70, 'pieces root': '1b4076e775071a3bcd3332ff6a0d8983f77dd737410cae9b056a0e7a1249fdec' } } },

//'meta version': 2, 'name': 'files', 'piece length': 16384 },
//'piece layers': { '1da5ee9b28e8fe3a02426621a52a4f59874e7d062fb922d5466cf544ab3d847d': '123ffea83f54c09f35fd2102edb6fb8a7ea6c4a6ae8f757ea54ca3d677fe1da8d99a522e56c60cb0114b54324b907acdc0c67b5cb993b1cf6f5b6f12b562c4b4ccf90b0053d20ad2fc437f02af6159ae919a5094a4

namespace xpx_storage_sdk {

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

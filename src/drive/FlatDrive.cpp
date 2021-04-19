/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "drive/FlatDrive.h"
#include "drive/Session.h"
#include "drive/ActionList.h"
#include "drive/Utils.h"
#include "drive/FsTree.h"
#include "drive/log.h"

#include <filesystem>
#include <set>
#include <iostream>
#include <thread>

#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>


namespace fs = std::filesystem;

namespace sirius { namespace drive {

//
// DrivePaths - drive paths, used at replicator side
//
class FlatDrivePaths {
protected:
    FlatDrivePaths( const std::string& replicatorRootFolder,
                const std::string& replicatorSandboxRootFolder,
                const std::string& drivePubKey )
        :
          m_drivePubKey( drivePubKey ),
          m_replicatorRoot( replicatorRootFolder ),
          m_replicatorSandboxRoot( replicatorSandboxRootFolder )
    {}

    virtual ~FlatDrivePaths() {}

protected:
    const std::string& m_drivePubKey;

    const fs::path  m_replicatorRoot;
    const fs::path  m_replicatorSandboxRoot;

    // Drive paths
    const fs::path  m_driveRootPath     = m_replicatorRoot / m_drivePubKey;
    const fs::path  m_driveFolder       = m_driveRootPath / "drive";
    const fs::path  m_torrentFolder     = fs::path( m_driveRootPath ) / "torrent";
    const fs::path  m_fsTreeFile        = fs::path( m_driveRootPath ) / "fs_tree" / FS_TREE_FILE_NAME;
    const fs::path  m_fsTreeTorrent     = fs::path( m_driveRootPath ) / "fs_tree" / FS_TREE_FILE_NAME ".torrent";

    // Sandbox paths
    const fs::path  m_sandboxRootPath       = m_replicatorSandboxRoot / m_drivePubKey;
    const fs::path  m_sandboxFsTreeFile     = m_sandboxRootPath / FS_TREE_FILE_NAME;
    const fs::path  m_sandboxFsTreeTorrent  = m_sandboxRootPath / FS_TREE_FILE_NAME ".torrent";

    // Client session data paths
    const fs::path  m_clientDataFolder      = m_sandboxRootPath / "client-data";
    const fs::path  m_clientDriveFolder     = m_clientDataFolder / "drive";
    const fs::path  m_clientActionListFile  = m_clientDataFolder / "ActionList.bin";

};

//
// DefaultDrive - it manages all user files at replicator side
//
class DefaultFlatDrive: public FlatDrive, protected FlatDrivePaths {

    using LtSession = std::shared_ptr<Session>;
    using lt_handle  = Session::lt_handle;

    struct FileExData {
        lt_handle m_ltHandle = {};
        bool      m_toBeRemoved = false;
    };

    LtSession     m_session;

    size_t        m_maxSize;
    endpoint_list m_otherReplicators;

    // FsTree
    FsTree        m_fsTree;
    FsTree        m_sandboxFsTree;
    lt_handle     m_fsTreeLtHandle; // used for removing FsTree torrent from session

    // Root hashes
    InfoHash      m_rootHash;
    InfoHash      m_resultRootHash;

    // Client data
    InfoHash      m_clientDataInfoHash;
    ActionList    m_actionList;

    // Will be called at the end of the sanbox work
    DriveModifyHandler m_modifyHandler;

    std::map<InfoHash,FileExData> m_fileMap;

public:

    DefaultFlatDrive( std::shared_ptr<Session>  session,
                  const std::string&        replicatorRootFolder,
                  const std::string&        replicatorSandboxRootFolder,
                  const std::string&        drivePubKey,
                  size_t                    maxSize,
                  const endpoint_list&      otherReplicators )
        :
          FlatDrivePaths( replicatorRootFolder, replicatorSandboxRootFolder, drivePubKey ),
          m_session(session),
          m_maxSize(maxSize),
          m_otherReplicators(otherReplicators)
    {
        // Initialize drive
        init();
    }

    virtual ~DefaultFlatDrive() {
        //TODO remove torrents
    }

    virtual InfoHash rootDriveHash() override {
        return m_rootHash;
    }

    // Initialize drive
    void init()
    {
        // Clear m_rootDriveHash
        memset( m_rootHash.data(), 0 , m_rootHash.size() );

        // Create nonexistent folders
        if ( !fs::exists( m_fsTreeFile ) )
        {
            if ( !fs::exists( m_driveFolder) ) {
                fs::create_directories( m_driveFolder );
            }

            if ( !fs::exists( m_torrentFolder) ) {
                fs::create_directories( m_torrentFolder );
            }
        }

        // Load FsTree
        if ( fs::exists(m_fsTreeFile) )
        {
            try {
                m_fsTree.deserialize( m_fsTreeFile );
            } catch(...) {
                fs::remove(m_fsTreeFile);
            }
        }

        // Create FsTree if it is absent
        if ( !fs::exists(m_fsTreeFile) )
        {
            buildFsTree();
            fs::create_directories( m_fsTreeFile.parent_path() );
            m_fsTree.doSerialize( m_fsTreeFile );
        }

        // Calculate torrent and root hash
        m_fsTree.deserialize( m_fsTreeFile );
        m_rootHash = createTorrentFile( m_fsTreeFile, m_replicatorRoot, m_fsTreeTorrent );

        //TODO compare rootHash with blockchain?

        // Add files to session
//        addFilesToSession( m_driveFolder, m_torrentFolder, m_fsTree );

        // Add FsTree to session
        m_fsTreeLtHandle = m_session->addTorrentFileToSession( m_fsTreeTorrent, m_fsTreeTorrent.parent_path(), m_otherReplicators );
    }

    // Build FsTree
    //
    void buildFsTree()
    {
        // Build FsTree recursively
        buildFsTree( m_driveFolder, m_torrentFolder, m_fsTree );
        m_fsTree.m_name = m_driveFolder.filename();
    }

    // Build FsTree recursively
    //
    void buildFsTree( fs::path folderPath, fs::path torrentFolderPath, Folder& fsTreeFolder )
    {
        // Check if 'torrent folder' exists
        if ( !fs::exists( torrentFolderPath ) )
        {
            fs::create_directories( torrentFolderPath );
        }

        // Loop by folder childs
        //
        for( const auto& child : std::filesystem::directory_iterator( folderPath ) )
        {
            // Child name
            auto name = child.path().filename();

            if ( child.is_directory() )
            {
                // Add subfolder
                buildFsTree( folderPath / name,
                             torrentFolderPath / name,
                             fsTreeFolder.getSubfolderOrCreate( name ) );
            }
            else if ( child.is_regular_file() )
            {
                // Add file
                //
                fs::path torrentFile = torrentFolderPath / name;

                // Get FsTree child
                Folder::Child* fsTreeChild = fsTreeFolder.findChild( name );

                // Throw error if it's folder
                if ( fsTreeChild != nullptr && isFolder(*fsTreeChild) ) {
                    throw std::runtime_error( std::string("attempt to create a file with existing folder with same name: ") + name.string() );
                }

                // Calculate torrent info
                InfoHash fileHash;
                if ( !fs::exists( torrentFile ) || fsTreeChild == nullptr ) {
                    fileHash = createTorrentFile( child.path(), m_driveRootPath, torrentFile );
                }

                // Add file into FsTree
                if ( fsTreeChild == nullptr ) {
                    size_t fileSize = std::filesystem::file_size( child.path() );
                    fsTreeFolder.m_childs.emplace_back( File{name,fileHash,fileSize} );
                    fsTreeChild = &fsTreeFolder.m_childs.back();
                }

                fileHash = getFile( *fsTreeChild ).m_hash;

                // skip equal files
                if ( m_fileMap.find(fileHash) != m_fileMap.end() )
                    continue;

                lt_handle ltHandle = m_session->addTorrentFileToSession( torrentFile,
                                                                         m_driveFolder,
                                                                         m_otherReplicators );
                m_fileMap[fileHash] = FileExData{ltHandle,false};
            }
        }
    }

    // add files to session recursively
    //
    void addFilesToSession( fs::path folderPath, fs::path torrentFolderPath, Folder& fsTreeFolder )
    {
        // Loop by folder childs
        //
        for( const auto& child : std::filesystem::directory_iterator( folderPath ) )
        {
            // Child name
            auto name = child.path().filename();

            if ( child.is_directory() )
            {
                // Get FsTree child
                Folder::Child* fsTreeChild = fsTreeFolder.findChild( name );

                if ( fsTreeChild == nullptr ) {
                    throw std::runtime_error( std::string("internal error, absent folder: ") + name.string() );
                }

                if ( isFile(*fsTreeChild) ) {
                    throw std::runtime_error( std::string("internal error, must be folder, filname: ") + name.string() );
                }

                // Go into subfolder
                addFilesToSession( folderPath / name,
                                   torrentFolderPath / name,
                                   getFolder(*fsTreeChild) );
            }
            else if ( child.is_regular_file() )
            {
                // Add file to session
                //

                // Get FsTree child
                Folder::Child* fsTreeChild = fsTreeFolder.findChild( name );

                if ( fsTreeChild == nullptr ) {
                    throw std::runtime_error( std::string("internal error absent file: ") + name.string() );
                }

                if ( isFolder(*fsTreeChild) ) {
                    throw std::runtime_error( std::string("attempt to create a file with existing folder with same name: ") + name.string() );
                }

                fs::path torrentFile = torrentFolderPath / name;
                if ( !fs::exists( torrentFile ) ) {
                    throw std::runtime_error( std::string("internal error absent torrent file: ") + name.string() );
                }
            }
        }
    }

    // startModifyDrive - should be called after client 'modify request'
    //
    void startModifyDrive( InfoHash modifyDataInfoHash, DriveModifyHandler modifyHandler ) override
    {
        using namespace std::placeholders;  // for _1, _2, _3

        m_clientDataInfoHash = modifyDataInfoHash;
        m_modifyHandler      = modifyHandler;

        // clear client session folder
        fs::remove_all( m_sandboxRootPath );
        fs::create_directories( m_sandboxRootPath);

        m_session->downloadFile( modifyDataInfoHash,
                                 m_sandboxRootPath,
                                 std::bind( &DefaultFlatDrive::downloadHandler, this, _1, _2, _3 ),
                                 m_otherReplicators );
    }

    // will be called by Session
    void downloadHandler( download_status::code code, const InfoHash& infoHash, const std::string& info )
    {
        if ( m_clientDataInfoHash != infoHash )
        {
            m_modifyHandler( modify_status::failed, infoHash, std::string("DefaultDrive::downloadHandler: internal error: ") + info );
            return;
        }

        if ( code == download_status::failed )
        {
            m_modifyHandler( modify_status::failed, InfoHash(), std::string("modify drive: download failed: ") + info );
            return;
        }

        if ( code == download_status::complete )
        {
            std::thread( [this] { modifyDriveInSandbox(); } ).detach();
        }
    }

    // client data is received,
    // so we start drive modification
    //
    void modifyDriveInSandbox()
    {
        // Check that client data exist
        if ( !fs::exists(m_clientDataFolder) || !fs::is_directory(m_clientDataFolder) )
        {
            LOG( "m_clientDataFolder=" << m_clientDataFolder );
            m_modifyHandler( modify_status::failed, InfoHash(), "modify drive: 'client-data' is absent: " );
        }

        // Check 'actionList.bin' is received
        if ( !fs::exists( m_clientActionListFile ) )
        {
            LOG( "m_clientActionListFile=" << m_clientActionListFile );
            m_modifyHandler( modify_status::failed, InfoHash(), "modify drive: 'ActionList.bin' is absent: " );
            return;
        }

        // Load 'actionList' into memory
        ActionList m_actionList;
        m_actionList.deserialize( m_clientActionListFile );

        // Make copy of current FsTree
        FsTree m_sandboxFsTree;
        m_sandboxFsTree.deserialize( m_fsTreeFile );

        //
        // Perform actions
        //
        for( const Action& action : m_actionList )
        {
            if (action.m_isInvalid)
                continue;

            switch( action.m_actionId )
            {
            //
            // Upload
            //
            case action_list_id::upload:
            {
                // Check that file exists in client folder
                fs::path clientFile = m_clientDriveFolder / action.m_param2;
                if ( !fs::exists( clientFile ) || fs::is_directory(clientFile) )
                {
                    action.m_isInvalid = true;
                    break;
                }

                // calculate torrent, file hash, and file size
                InfoHash fileHash = calculateInfoHashAndTorrent( clientFile, m_drivePubKey, m_torrentFolder );
                size_t fileSize = std::filesystem::file_size( clientFile );

                // rename file and place it into drive
                std::string newFileName = internalFileName( fileHash );
                fs::rename( clientFile, m_driveFolder / newFileName );

                // add file in resultFsTree
                m_sandboxFsTree.addFile( fs::path(action.m_param2).parent_path(),
                                       clientFile.filename(),
                                       fileHash,
                                       fileSize );

                // add ref to 'fileMap'
                m_fileMap.try_emplace( fileHash, FileExData{} );

                break;
            }
            //
            // New folder
            //
            case action_list_id::new_folder:
            {
                // Check that entry is free
                if ( m_sandboxFsTree.findChild( action.m_param1 ) != nullptr )
                {
                    //todo m_isInvalid?
                    action.m_isInvalid = true;
                    break;
                }

                m_sandboxFsTree.addFolder( action.m_param2 );
                break;
            }
            //
            // Move
            //
            case action_list_id::move:
            {
                auto* srcChild = m_sandboxFsTree.getEntryPtr( action.m_param1 );

                // Check that src child exists
                if ( srcChild == nullptr )
                {
                    LOG( "invalid 'move' action: src not exists (in FsTree): " << action.m_param1  );
                    action.m_isInvalid = true;
                    break;
                }

                // Check topology (nesting folders)
                if ( isFolder(*srcChild) )
                {
                    fs::path srcPath = fs::path("root") / action.m_param1;
                    fs::path destPath = fs::path("root") / action.m_param2;

                    // srcPath should not be a parent folder of destPath
                    if ( isPathInsideFolder( destPath, srcPath ) )
                    {
                        LOG( "invalid 'move' action: 'srcPath' is a directory which is an ancestor of 'destPath'" );
                        LOG( "invalid 'move' action: srcPath : " << action.m_param1  );
                        LOG( "invalid 'move' action: destPath : " << action.m_param2  );
                        action.m_isInvalid = true;
                        break;
                    }
                }

                // modify FsTree
                m_sandboxFsTree.moveFlat( action.m_param1, action.m_param2, [this] ( const InfoHash& fileHash )
                {
                    m_fileMap.try_emplace( fileHash, FileExData{} );
                } );

                break;
            }
            //
            // Remove
            //
            case action_list_id::remove: {

                if ( m_sandboxFsTree.findChild( action.m_param1 ) == nullptr )
                {
                    LOG( "invalid 'remove' action: src not exists (in FsTree): " << action.m_param1  );
                    action.m_isInvalid = true;
                    break;
                }

                // remove entry from FsTree
                m_sandboxFsTree.removeFlat( action.m_param1, [this] ( const InfoHash& fileHash )
                {
                    m_fileMap.try_emplace( fileHash, FileExData{} );
                } );

                break;
            }

            } // end of switch()
        } // end of for( const Action& action : m_actionList )

        // calculate new rootHash
        m_sandboxFsTree.doSerialize( m_sandboxFsTreeFile );
        m_resultRootHash = createTorrentFile( m_sandboxFsTreeFile, m_sandboxRootPath, m_sandboxFsTreeTorrent );

        // Call handler
        m_modifyHandler( modify_status::sandbox_root_hash, m_resultRootHash, "" );

        // start drive update
        updateDrive_1();
    }
    
    // updates drive (1st step after aprove)
    // - remove torrents from session
    //
    void updateDrive_1()
    {
        // Prepare map (m_toBeRemoved = true)
        for( auto& it : m_fileMap )
            it.second.m_toBeRemoved = true;

        // Set m_toBeRemoved = false
        markUsedFiles( m_fsTree );


        // Remove unused files from session
        for( const auto& it : m_fileMap )
        {
            const FileExData& info = it.second;
            if ( info.m_toBeRemoved )
            {
                if ( info.m_ltHandle.is_valid() )
                    m_session->removeTorrentFromSession( info.m_ltHandle );
            }
        }

        // Remove FsTree torrent
        m_session->removeTorrentFromSession( m_fsTreeLtHandle, [this]
        {
            // remove files
            fs::remove( m_fsTreeFile );
            fs::remove( m_fsTreeTorrent );

            updateDrive_2();
        });
    }

    // updates drive (2st phase after fsTree torrent removed)
    // - remove unused files and torrent files
    // - add new torrents to session
    //
    void updateDrive_2() try
    {
        // update FsTree file & torrent
        fs::rename( m_sandboxFsTreeFile, m_fsTreeFile );
        fs::rename( m_sandboxFsTreeTorrent, m_fsTreeTorrent );

        // clear sandbox
        fs::remove_all( m_sandboxRootPath );

        // remove unused files and torrents from drive
        for( const auto& it : m_fileMap )
        {
            const FileExData& info = it.second;
            if ( info.m_toBeRemoved )
            {
                const auto& hash = it.first;
                std::string filename = internalFileName( hash );
                fs::remove( fs::path(m_driveFolder) / filename );
                fs::remove( fs::path(m_torrentFolder) / filename );
            }
        }

        // remove unused data from 'fileMap'
        std::erase_if( m_fileMap, [] (const auto& it) { return it.second.m_toBeRemoved; } );

        //
        // Add torrents to session
        //
        for( auto& it : m_fileMap )
        {
            if ( !it.second.m_ltHandle.is_valid() )
            {
                std::string fileName = internalFileName( it.first );
                m_fsTreeLtHandle = m_session->addTorrentFileToSession( m_torrentFolder / fileName,
                                                                       m_torrentFolder,
                                                                       m_otherReplicators );
            }
        }

        // Add FsTree torrent to session
        m_fsTreeLtHandle = m_session->addTorrentFileToSession( m_fsTreeTorrent, m_fsTreeTorrent.parent_path(), m_otherReplicators );

        // Call update handler
        m_modifyHandler( modify_status::update_completed, InfoHash(), "" );
    }
    catch ( std::exception ex )
    {
        LOG( "!ERROR!: updateDrive_2 error: " << ex.what() );
        exit(-1);
    }

    // Recursively marks 'm_toBeRemoved' as false
    //
    void markUsedFiles( const Folder& folder )
    {
        for( const auto& child : folder.m_childs )
        {
            if ( isFolder(child) )
            {
                markUsedFiles( getFolder(child) );
            }
            else
            {
                auto& hash = getFile(child).m_hash;
                const auto& it = m_fileMap.find(hash);
                if ( it != m_fileMap.end() )
                {
                    it->second.m_toBeRemoved = false;
                }
                else
                {
                    LOG( "markUsedFiles: internal error");
                }
            }
        }
    }

    // Now it holds only liteners errors
    //
//    static void errorHandler( Session*, libtorrent::alert* alert )
//    {
//        if ( alert->type() == lt::listen_failed_alert::alert_type )
//        {
//            std::cerr << "replicator socket error: " << alert->message() << std::endl << std::flush;
//            exit(-1); //TODO
//        }
//    }
};


std::shared_ptr<FlatDrive> createDefaultFlatDrive(
        std::shared_ptr<Session> session,
        const std::string&       replicatorRootFolder,
        const std::string&       replicatorSandboxRootFolder,
        const std::string&       drivePubKey,
        size_t                   maxSize,
        const endpoint_list&     otherReplicators )
{
    return std::make_shared<DefaultFlatDrive>( session,
                                           replicatorRootFolder,
                                           replicatorSandboxRootFolder,
                                           drivePubKey,
                                           maxSize,
                                           otherReplicators );
}

}}

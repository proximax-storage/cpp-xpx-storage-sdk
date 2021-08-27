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
    const std::string m_drivePubKey;

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
    const fs::path  m_clientActionListFile  = m_clientDataFolder / "actionList.bin";

};

//
// DefaultDrive - it manages all user files at replicator side
//
class DefaultFlatDrive: public FlatDrive, protected FlatDrivePaths {

    using LtSession = std::shared_ptr<Session>;
    using lt_handle  = Session::lt_handle;

    // TorrentExData is used to avoid adding torrents into session with the same hash
    // and for deleting unused files and torrents from session
    struct TorrentExData {
        lt_handle m_ltHandle = {};
        bool      m_isUnused = false;
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
    InfoHash      m_sandboxRootHash;

    // Client data
    InfoHash      m_clientDataInfoHash;

    std::vector<InfoHash> m_toBeAddedFiles;

    // Will be called at the end of the sanbox work
    DriveModifyHandler m_modifyHandler;

    // TorrentHandleMap is used to avoid adding torrents into session with the same hash
    // and for deleting unused files and torrents from session
    std::map<InfoHash,TorrentExData> m_torrentHandleMap;

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
        terminate();
    }

    void terminate() {
        //TODO 
        m_session.reset();
        if ( m_modifyHandler )
        {
            m_modifyHandler( modify_status::broken, InfoHash(), std::string("DefaultDrive::downloadHandler: internal error: ") );
            m_modifyHandler = nullptr;
        }
        //m_session->endSession();
    }

    virtual InfoHash rootDriveHash() override {
        LOG( "m_drivePubKey: " << m_drivePubKey );
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
            fs::create_directories( m_fsTreeFile.parent_path() );
            m_fsTree.m_name = "/";
            m_fsTree.doSerialize( m_fsTreeFile );
        }

        // Calculate torrent and root hash
        m_fsTree.deserialize( m_fsTreeFile );
//        m_fsTree.addFolder("x");
//        m_fsTree.doSerialize( m_fsTreeFile );
//        m_fsTree.dbgPrint();
        m_rootHash = createTorrentFile( m_fsTreeFile, m_fsTreeFile.parent_path(), m_fsTreeTorrent );
        //todo!!!
//        m_fsTree.remove("x");
//        m_fsTree.addFolder("y");
//        m_fsTree.doSerialize( m_fsTreeFile );
//        m_fsTree.dbgPrint();

        //TODO compare rootHash with blockchain?

        // Add files to session
//        addFilesToSession( m_driveFolder, m_torrentFolder, m_fsTree );

        // Add FsTree to session
        m_fsTreeLtHandle = m_session->addTorrentFileToSession( m_fsTreeTorrent,
                                                               m_fsTreeTorrent.parent_path(),
                                                               lt::sf_is_replicator,
                                                               m_otherReplicators );
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
    void startModifyDrive( InfoHash          modifyDataInfoHash,
                          const Hash256&     transactionHash,
                          uint64_t           maxDataSize,
                          DriveModifyHandler modifyHandler ) override
    {
        using namespace std::placeholders;  // for _1, _2, _3

        m_clientDataInfoHash = modifyDataInfoHash;
        m_modifyHandler      = modifyHandler;

        // clear client session folder
        fs::remove_all( m_sandboxRootPath );
        fs::create_directories( m_sandboxRootPath);

        m_session->download( DownloadContext(
                                            DownloadContext::client_data,
                                            std::bind( &DefaultFlatDrive::downloadHandler, this, _1, _2, _3, _4, _5, _6 ),
                                            modifyDataInfoHash,
                                            transactionHash,
                                            0, //todo
                                            ""),
                                       m_sandboxRootPath,
                                       m_otherReplicators );
    }

    // will be called by Session
    void downloadHandler( download_status::code code,
                          const InfoHash& infoHash,
                          const std::filesystem::path /*filePath*/,
                          size_t /*downloaded*/,
                          size_t /*fileSize*/,
                          const std::string& errorText )
    {
        if ( m_clientDataInfoHash != infoHash )
        {
            m_modifyHandler( modify_status::failed, infoHash, std::string("DefaultDrive::downloadHandler: internal error: ") );
            m_modifyHandler = nullptr;
            return;
        }

        if ( code == download_status::failed )
        {
            m_modifyHandler( modify_status::failed, infoHash, std::string("modify drive: download failed: ") + errorText );
            m_modifyHandler = nullptr;
            return;
        }

        if ( code == download_status::complete )
        {
            std::thread( [this] { this->modifyDriveInSandbox(); } ).detach();
        }
    }

    // client data is received,
    // so we start drive modification
    //
    void modifyDriveInSandbox()
    {
        //LOG("++++++++++++++++++++++ modifyDriveInSandbox");

        // Check that client data exist
        if ( !fs::exists(m_clientDataFolder) || !fs::is_directory(m_clientDataFolder) )
        {
            LOG( "m_clientDataFolder=" << m_clientDataFolder );
            m_modifyHandler( modify_status::failed, InfoHash(), "modify drive: 'client-data' is absent: " );
            m_modifyHandler = nullptr;
            return;
        }

        // Check 'actionList.bin' is received
        if ( !fs::exists( m_clientActionListFile ) )
        {
            LOG( "m_clientActionListFile=" << m_clientActionListFile );
            m_modifyHandler( modify_status::failed, InfoHash(), "modify drive: 'ActionList.bin' is absent: " );
            m_modifyHandler = nullptr;
            return;
        }

        // Load 'actionList' into memory
        ActionList actionList;
        actionList.deserialize( m_clientActionListFile );

        // Make copy of current FsTree
        m_sandboxFsTree.deserialize( m_fsTreeFile );

        //
        // Perform actions
        //
        for( const Action& action : actionList )
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
                InfoHash fileHash = calculateInfoHashAndTorrent( clientFile, m_drivePubKey, m_torrentFolder, "" );
                size_t fileSize = std::filesystem::file_size( clientFile );

                // rename file and move it into drive folder
                std::string newFileName = m_driveFolder / internalFileName( fileHash );
                fs::rename( clientFile, newFileName );

                // add file in resultFsTree
                m_sandboxFsTree.addFile( fs::path(action.m_param2).parent_path(),
                                       clientFile.filename(),
                                       fileHash,
                                       fileSize );

                m_toBeAddedFiles.emplace_back( fileHash );

                // add ref into 'torrentMap'
                m_torrentHandleMap.try_emplace( fileHash, TorrentExData{} );

                break;
            }
            //
            // New folder
            //
            case action_list_id::new_folder:
            {
                // Check that entry is free
                if ( m_sandboxFsTree.getEntryPtr( action.m_param1 ) != nullptr )
                {
                    //todo m_isInvalid?
                    action.m_isInvalid = true;
                    break;
                }

                m_sandboxFsTree.addFolder( action.m_param1 );
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
                    if ( isPathInsideFolder( srcPath, destPath ) )
                    {
                        LOG( "invalid 'move' action: 'srcPath' is a directory which is an ancestor of 'destPath'" );
                        LOG( "invalid 'move' action: srcPath : " << action.m_param1  );
                        LOG( "invalid 'move' action: destPath : " << action.m_param2  );
                        action.m_isInvalid = true;
                        break;
                    }
                }

                // modify FsTree
                m_sandboxFsTree.moveFlat( action.m_param1, action.m_param2, [/*this*/] ( const InfoHash& /*fileHash*/ )
                {
                    //m_torrentMap.try_emplace( fileHash, TorrentExData{} );
                } );

                break;
            }
            //
            // Remove
            //
            case action_list_id::remove: {

                if ( m_sandboxFsTree.getEntryPtr( action.m_param1 ) == nullptr )
                {
                    LOG( "invalid 'remove' action: src not exists (in FsTree): " << action.m_param1  );
                    m_sandboxFsTree.dbgPrint();
                    action.m_isInvalid = true;
                    break;
                }

                // remove entry from FsTree
                m_sandboxFsTree.removeFlat( action.m_param1, [this] ( const InfoHash& fileHash )
                {
                    m_torrentHandleMap.try_emplace( fileHash, TorrentExData{} );
                } );

                break;
            }

            } // end of switch()
        } // end of for( const Action& action : actionList )

        // calculate new rootHash
        m_sandboxFsTree.doSerialize( m_sandboxFsTreeFile );
        m_sandboxRootHash = createTorrentFile( m_sandboxFsTreeFile, m_sandboxRootPath, m_sandboxFsTreeTorrent );

        // Call modify handler
        m_modifyHandler( modify_status::sandbox_root_hash, m_sandboxRootHash, "" );

        // start drive update
        updateDrive_1();
    }

    // updates drive (1st step after aprove)
    // - remove torrents from session
    //
    void updateDrive_1()
    {
        // Prepare map (m_isUnused = true) for detecting of used files
        for( auto& it : m_torrentHandleMap )
            it.second.m_isUnused = true;

        // Mark used files
        markUsedFiles( m_sandboxFsTree );

        // Prepare set<> for to be removed torrents
        std::set<lt::torrent_handle> toBeRemovedTorrents;

        // Add unused files into set<>
        for( const auto& it : m_torrentHandleMap )
        {
            const TorrentExData& info = it.second;
            if ( info.m_isUnused )
            {
                if ( info.m_ltHandle.is_valid() )
                    toBeRemovedTorrents.insert( info.m_ltHandle );
            }
        }

        // Add current fsTree torrent handle
        toBeRemovedTorrents.insert( m_fsTreeLtHandle );

        // Remove unused torrents
        m_session->removeTorrentsFromSession( std::move(toBeRemovedTorrents), [this]{ updateDrive_2(); } );
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
        m_fsTree = m_sandboxFsTree;
        m_rootHash = m_sandboxRootHash;

        // clear sandbox
        fs::remove_all( m_sandboxRootPath );

        // remove unused files and torrent files from the drive
        for( const auto& it : m_torrentHandleMap )
        {
            const TorrentExData& info = it.second;
            if ( info.m_isUnused )
            {
                const auto& hash = it.first;
                std::string filename = internalFileName( hash );
                fs::remove( fs::path(m_driveFolder) / filename );
                fs::remove( fs::path(m_torrentFolder) / filename );
                LOG("+++ updateDrive_2: removed: " << filename );
            }
        }

        // remove unused data from 'fileMap'
        std::erase_if( m_torrentHandleMap, [] (const auto& it) { return it.second.m_isUnused; } );

        //
        // Add torrents into session
        //
        for( auto& it : m_torrentHandleMap )
        {
            if ( !it.second.m_ltHandle.is_valid() )
            {
                std::string fileName = internalFileName( it.first );
                m_fsTreeLtHandle = m_session->addTorrentFileToSession( m_torrentFolder / fileName,
                                                                       m_driveFolder,
                                                                       lt::sf_is_replicator,
                                                                       m_otherReplicators );
            }
        }

        // Add new files
        for( const auto& fileHash: m_toBeAddedFiles )
        {
            fs::path torrentFile = m_torrentFolder / internalFileName(fileHash);
            m_session->addTorrentFileToSession( torrentFile, m_driveFolder, lt::sf_is_replicator, {} );
        }
        m_toBeAddedFiles.clear();

        // Add FsTree torrent to session
        m_fsTreeLtHandle = m_session->addTorrentFileToSession( m_fsTreeTorrent,
                                                               m_fsTreeTorrent.parent_path(),
                                                               lt::sf_is_replicator,
                                                               m_otherReplicators );

        // Call update handler
        m_modifyHandler( modify_status::update_completed, InfoHash(), "" );
        m_modifyHandler = nullptr;
    }
    catch ( const std::exception& ex )
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
                const auto& it = m_torrentHandleMap.find(hash);
                if ( it != m_torrentHandleMap.end() )
                {
                    it->second.m_isUnused = false;
                }
                else
                {
                    LOG( "markUsedFiles: internal error");
                }
            }
        }
    }

    void     loadTorrent( const InfoHash& /*fileHash*/ ) override
    {
        //todo m_session->loadTorrent();
    }

    virtual void printDriveStatus() override
    {
        LOG("Drive Status:")
        m_fsTree.dbgPrint();
        m_session->printActiveTorrents();
    }

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

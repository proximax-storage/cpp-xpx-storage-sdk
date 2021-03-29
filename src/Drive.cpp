/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "Drive.h"
#include "LibTorrentSession.h"
#include "ActionList.h"
#include <filesystem>
#include <iostream>
#include <thread>

#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>

namespace fs = std::filesystem;

namespace sirius { namespace drive {

//
// DrivePaths - drive paths, used at replicator side
//
class DrivePaths {
protected:
    DrivePaths( std::string replicatorRootFolder,
                std::string replicatorSandboxRootFolder,
                std::string drivePubKey )
        :
          m_drivePubKey( drivePubKey ),
          m_replicatorRoot( replicatorRootFolder ),
          m_replicatorSandboxRoot( replicatorSandboxRootFolder )
    {}

protected:
    std::string     m_drivePubKey;
    const fs::path  m_replicatorRoot;
    const fs::path  m_replicatorSandboxRoot;

    const fs::path  m_driveRootPath     = m_replicatorRoot / m_drivePubKey;
    const fs::path  m_driveFolder       = m_driveRootPath / "drive";
    const fs::path  m_torrentFolder     = fs::path( m_driveRootPath ) / "torrent";
    const fs::path  m_fsTreeFile        = fs::path( m_driveRootPath ) / FS_TREE_FILE_NAME;
    const fs::path  m_fsTreeTorrent     = fs::path( m_driveRootPath ) / "FsTree.torrent";

    const fs::path  m_sandboxRootPath       = m_replicatorSandboxRoot / m_drivePubKey;
    const fs::path  m_clientDataFolder      = m_sandboxRootPath / "client-data";
    const fs::path  m_sandboxDriveFolder    = m_sandboxRootPath / "drive";
    const fs::path  m_sandboxTorrentFolder  = m_sandboxRootPath / "torrent";
    const fs::path  m_sandboxFsTreeFile     = m_sandboxRootPath / FS_TREE_FILE_NAME;
    const fs::path  m_sandboxFsTreeTorrent  = m_sandboxRootPath / "FsTree.torrent";
    const fs::path  m_sandboxActionListFile = m_sandboxRootPath / "ActionList.bin";
};

//
// DefaultDrive - it manages all user files at replicator side
//
class DefaultDrive: public Drive, protected DrivePaths {

    using LtSession = std::shared_ptr<LibTorrentSession>;
    using lt_handle  = LibTorrentSession::lt_handle;

    LtSession     m_session;
    std::string   m_listenInterface;
    size_t        m_maxSize;
    endpoint_list m_otherReplicators;

    // FsTree
    FsTree        m_fsTree;
    FsTree        m_resultFsTree;
    lt_handle     m_fsTreeLtHandle;

    // Root hashes
    InfoHash      m_rootHash;
    InfoHash      m_resultRootHash;

    // Client data
    InfoHash      m_clientDataInfoHash;
    ActionList    m_actionList;

    // Will be called at the end of the sanbox work
    DriveModifyHandler m_modifyHandler;

public:

    DefaultDrive( std::string listenInterface,
                  std::string replicatorRootFolder,
                  std::string replicatorSandboxRootFolder,
                  std::string drivePubKey,
                  size_t      maxSize,
                  endpoint_list otherReplicators )
        :
          DrivePaths( replicatorRootFolder, replicatorSandboxRootFolder, drivePubKey ),
          m_listenInterface(listenInterface),
          m_maxSize(maxSize),
          m_otherReplicators(otherReplicators)
    {
        // Initialize drive
        init();
    }

    virtual ~DefaultDrive() {
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

        // Start drive session
        startDistributionSession();

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
        //m_session->addTorrentFileToSession( m_fsTreeTorrent, m_replicatorRoot, m_otherReplicators );
        m_fsTreeLtHandle = m_session->addTorrentFileToSession( m_fsTreeTorrent, m_fsTreeTorrent.parent_path(), m_otherReplicators );
    }


    // Start libtorrent session with 'torrent files'
    //
    void startDistributionSession()
    {
        using namespace std::placeholders;  // for _1, _2

        m_session = createDefaultLibTorrentSession( m_listenInterface,
                                                    std::bind( &DefaultDrive::alertHandler, this, _1, _2 ) );
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

                getFile( *fsTreeChild ).m_ltHandle = m_session->addTorrentFileToSession( torrentFile,
                                                                                         torrentFile.parent_path(),
                                                                                         m_otherReplicators );
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

                if ( !isFolder(*fsTreeChild) ) {
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

        // clear tmp folder
        fs::remove_all( m_sandboxDriveFolder.parent_path() );
        fs::create_directories( m_sandboxRootPath);

        m_session->downloadFile( modifyDataInfoHash,
                                 m_sandboxRootPath,
                                 std::bind( &DefaultDrive::downloadHandler, this, _1, _2, _3 ),
                                 m_otherReplicators );
    }

    // will be called by Session
    void downloadHandler( download_status::code code, InfoHash infoHash, std::string info )
    {
        if ( m_clientDataInfoHash != infoHash )
        {
            m_modifyHandler( modify_status::failed, InfoHash(), std::string("DefaultDrive::downloadHandler: internal error: ") + info );
            return;
        }

        if ( code == download_status::failed )
        {
            m_modifyHandler( modify_status::failed, InfoHash(), std::string("modify drive: download failed: ") + info );
            return;
        }

        if ( code == download_status::complete )
        {
            std::thread( [this] { modifyInSandbox(); } ).detach();
        }
    }

    // client data is received,
    // so we start drive modification
    //
    void modifyInSandbox()
    {
        // Check client data
        if ( !fs::exists(m_clientDataFolder) || !fs::is_directory(m_clientDataFolder) )
        {
            LOG( "m_clientDataFolder=" << m_clientDataFolder );
            m_modifyHandler( modify_status::failed, InfoHash(), "modify drive: 'client-data' is absent: " );
        }

        // Move client data to proper folder
        for( fs::path it: fs::directory_iterator(m_clientDataFolder) )
        {
                fs::rename( it, m_sandboxRootPath / it.filename() );
        }

        // Check 'action list' is received in client data
        if ( !fs::exists( m_sandboxActionListFile ) )
        {
            LOG( "m_sandboxActionListFile=" << m_sandboxActionListFile );
            m_modifyHandler( modify_status::failed, InfoHash(), "modify drive: 'ActionList.bin' is absent: " );
            return;
        }

        // Load actionList into our structure
        ActionList m_actionList;
        m_actionList.deserialize( m_sandboxActionListFile );

        // Copy current resultFsTree
        FsTree m_resultFsTree;
        m_resultFsTree.deserialize( m_fsTreeFile );

        //
        // Perform actions
        //
        for( const Action& action : m_actionList )
        {
            switch( action.m_actionId )
            {
            //
            // Upload
            //
            case action_list_id::upload: {

                // file path and torrentfile path
                fs::path file = m_sandboxDriveFolder / action.m_param2;
                //LOG( "upload file:   " << file );
                fs::path torrentFile = m_sandboxTorrentFolder / action.m_param2;
                //LOG( "upload torrent:" << torrentFile );

                // calculate torrent, hash, and size
                if ( fs::exists(torrentFile) )
                {
                    // Skip duplicate add actions
                }
                else
                {
                    fs::create_directories( torrentFile.parent_path() );
                    InfoHash infoHash = createTorrentFile( file, m_sandboxRootPath, torrentFile );
                    size_t fileSize = std::filesystem::file_size( file );

                    // add file in resultFsTree
                    m_resultFsTree.addFile( fs::path(action.m_param2).parent_path(),
                                           file.filename(),
                                           infoHash,
                                           fileSize );
                }
                break;
            }
            //
            // New folder
            //
            case action_list_id::new_folder:
                m_resultFsTree.addFolder( action.m_param2 );
                break;
            //
            // Move
            //
            case action_list_id::move: {
                if ( fs::exists( m_driveFolder / action.m_param1 ) )
                {
                    // file path and torrentfile path
                    fs::path file = m_sandboxDriveFolder / action.m_param2;
                    fs::path torrentFile = m_sandboxTorrentFolder / action.m_param2;
                    fs::copy( m_driveFolder / action.m_param1, file );

                    fs::create_directories( torrentFile.parent_path() );
                    InfoHash infoHash = createTorrentFile( file, m_sandboxRootPath, torrentFile );

                    m_resultFsTree.move( action.m_param1, action.m_param2, &infoHash );
                }
                break;
            }
            //
            // Remove
            //
            case action_list_id::remove:
                m_resultFsTree.remove( action.m_param1 );
                break;
            }
        }

        // calculate new rootHash
        m_resultFsTree.doSerialize( m_sandboxFsTreeFile );
        m_resultRootHash = createTorrentFile( m_sandboxFsTreeFile, m_sandboxRootPath, m_sandboxFsTreeTorrent );

        // start drive update
        updateDrive_1();
    }

    // updates drive (1st phase)
    // - removes all moved or removed torents
    // - removes current fsTree torrent
    //
    void updateDrive_1()
    {
        // Remove torrents
        for( const Action& action : m_actionList )
        {
            switch( action.m_actionId )
            {
            case action_list_id::upload:
            case action_list_id::new_folder:
                break;
            case action_list_id::move:
            case action_list_id::remove:
                m_session->removeTorrentFromSession( action.m_ltHandle );
                break;
            }
        }

        // Remove FsTree torrent
        m_session->removeTorrentFromSession( m_fsTreeLtHandle, [this] { updateDrive_2(); } );
    }

    // updates drive (2st phase)
    // - removes all moved or removed files
    // - add new files
    // - add new torrents
    //
    void updateDrive_2()
    {
        //
        // Remove files
        //
        for( const Action& action : m_actionList )
        {
            switch( action.m_actionId )
            {
            case action_list_id::upload:
            case action_list_id::new_folder:
                break;
            case action_list_id::move:
            case action_list_id::remove:
                fs::remove_all( m_driveFolder / action.m_param1 );
                fs::remove_all( m_torrentFolder / action.m_param1 );
                break;
            }
        }

        //
        // Move new files
        //
        m_rootHash = m_resultRootHash;
        fs::rename( m_sandboxDriveFolder, m_driveFolder );
        fs::rename( m_sandboxTorrentFolder, m_torrentFolder );
        fs::rename( m_sandboxFsTreeFile, m_fsTreeFile );
        fs::rename( m_sandboxFsTreeTorrent, m_fsTreeTorrent );
        fs::remove_all( m_sandboxRootPath );

        //
        // Add torrents
        //
        for( const Action& action : m_actionList )
        {
            switch( action.m_actionId )
            {
            case action_list_id::upload:
            case action_list_id::move: {

                auto child = m_resultFsTree.findChild( fs::path( action.m_param2 ) );

                if ( child != nullptr && !isFolder(*child) )
                {
                    File& fsTreeFile = getFile(*child);
                    fs::path file = m_driveFolder / action.m_param2;
                    fs::path torrentFile = m_torrentFolder / action.m_param2;

                    fsTreeFile.m_ltHandle = m_session->addTorrentFileToSession( torrentFile, m_driveFolder );
                }
                break;
            }
            case action_list_id::new_folder:
                break;
            case action_list_id::remove:
                break;
            }
        }

        m_fsTreeLtHandle = m_session->addTorrentFileToSession( m_fsTreeTorrent, m_fsTreeTorrent.parent_path(), m_otherReplicators );

        // Call update handler
        m_modifyHandler( modify_status::update_completed, InfoHash(), "" );
    }

    void alertHandler( LibTorrentSession*, libtorrent::alert* )
    {

    }
};


std::shared_ptr<Drive> createDefaultDrive(
        std::string listenInterface,
        std::string replicatorRootFolder,
        std::string replicatorSandboxRootFolder,
        std::string drivePubKey,
        size_t      maxSize,
        endpoint_list otherReplicators )
{
    return std::make_shared<DefaultDrive>( listenInterface,
                                           replicatorRootFolder,
                                           replicatorSandboxRootFolder,
                                           drivePubKey,
                                           maxSize,
                                           otherReplicators );
}

}}

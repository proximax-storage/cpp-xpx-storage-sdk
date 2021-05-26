/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "drive/Drive.h"
#include "drive/Session.h"
#include "drive/ActionList.h"
#include "drive/Utils.h"

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
class DrivePaths {
protected:
    DrivePaths( const std::string& replicatorRootFolder,
                const std::string& replicatorSandboxRootFolder,
                const std::string& drivePubKey )
        :
          m_drivePubKey( drivePubKey ),
          m_replicatorRoot( replicatorRootFolder ),
          m_replicatorSandboxRoot( replicatorSandboxRootFolder )
    {}

protected:
    const std::string& m_drivePubKey;

    const fs::path  m_replicatorRoot;
    const fs::path  m_replicatorSandboxRoot;

    // Drive paths
    const fs::path  m_driveRootPath     = m_replicatorRoot / m_drivePubKey;
    const fs::path  m_driveFolder       = m_driveRootPath / "drive";
    const fs::path  m_torrentFolder     = fs::path( m_driveRootPath ) / "torrent";
    const fs::path  m_fsTreeFile        = fs::path( m_driveRootPath ) / FS_TREE_FILE_NAME;
    const fs::path  m_fsTreeTorrent     = fs::path( m_driveRootPath ) / "FsTree.torrent";

    // Sandbox paths
    const fs::path  m_sandboxRootPath       = m_replicatorSandboxRoot / m_drivePubKey;
    const fs::path  m_sandboxDriveFolder    = m_sandboxRootPath / "drive";
    const fs::path  m_sandboxTorrentFolder  = m_sandboxRootPath / "torrent";
    const fs::path  m_sandboxFsTreeFile     = m_sandboxRootPath / FS_TREE_FILE_NAME;
    const fs::path  m_sandboxFsTreeTorrent  = m_sandboxRootPath / "FsTree.torrent";

    // Client data paths
    const fs::path  m_clientDataFolder      = m_sandboxRootPath / "client-data";
    const fs::path  m_clientDriveFolder     = m_clientDataFolder / "drive";
    const fs::path  m_clientActionListFile  = m_clientDataFolder / "ActionList.bin";

};

//
// DefaultDrive - it manages all user files at replicator side
//
class DefaultDrive: public Drive, protected DrivePaths {

    using LtSession = std::shared_ptr<Session>;
    using lt_handle  = Session::lt_handle;

    LtSession     m_session;

    size_t        m_maxSize;
    endpoint_list m_otherReplicators;

    // FsTree
    FsTree        m_fsTree;
    FsTree        m_sandboxFsTree;
    lt_handle     m_fsTreeLtHandle; // used for removing FsTree torrent from session
    std::set<std::string> m_toBeRemovedEntries;

    // Root hashes
    InfoHash      m_rootHash;
    InfoHash      m_resultRootHash;

    // Client data
    InfoHash      m_clientDataInfoHash;
    ActionList    m_actionList;

    // Will be called at the end of the sanbox work
    DriveModifyHandler m_modifyHandler;

public:

    DefaultDrive( std::shared_ptr<Session>  session,
                  const std::string&        replicatorRootFolder,
                  const std::string&        replicatorSandboxRootFolder,
                  const std::string&        drivePubKey,
                  size_t                    maxSize,
                  const endpoint_list&      otherReplicators )
        :
          DrivePaths( replicatorRootFolder, replicatorSandboxRootFolder, drivePubKey ),
          m_session(session),
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
        m_toBeRemovedEntries.clear();

        m_session->downloadFile( modifyDataInfoHash,
                                 m_sandboxRootPath,
                                 std::bind( &DefaultDrive::downloadHandler, this, _1, _2, _3 ),
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
        // Check client data
        if ( !fs::exists(m_clientDataFolder) || !fs::is_directory(m_clientDataFolder) )
        {
            LOG( "m_clientDataFolder=" << m_clientDataFolder );
            m_modifyHandler( modify_status::failed, InfoHash(), "modify drive: 'client-data' is absent: " );
        }

        // Check 'action list' is received in client data
        if ( !fs::exists( m_clientActionListFile ) )
        {
            LOG( "m_clientActionListFile=" << m_clientActionListFile );
            m_modifyHandler( modify_status::failed, InfoHash(), "modify drive: 'ActionList.bin' is absent: " );
            return;
        }

        // Load actionList into memory
        m_actionList.deserialize( m_clientActionListFile );

        // Get current copy of FsTree
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
            case action_list_id::upload: {

                // Check that file exists
                fs::path clientFile = m_clientDriveFolder / action.m_param2;
                if ( !fs::exists( clientFile ) || fs::is_directory(clientFile) )
                {
                    action.m_isInvalid = true;
                    break;
                }

                // Path file in sandbox
                fs::path sandboxFilePath = m_sandboxDriveFolder / action.m_param2;

                // if the file already exists in sandbox, remove it
                if ( fs::exists( sandboxFilePath ) )
                {
                    fs::remove_all( sandboxFilePath );
                }

                // Move file to sandbox
                fs::create_directories( sandboxFilePath.parent_path() );
                fs::copy( clientFile, m_sandboxDriveFolder / sandboxFilePath );

                // torrentfile path
                fs::path torrentFile = m_sandboxTorrentFolder / action.m_param2;

                // if torrentfile in sandbox already exists we remove it
                if ( fs::exists(torrentFile) )
                {
                    fs::remove_all( torrentFile );
                }

                // calculate torrent, hash, and size
                fs::create_directories( torrentFile.parent_path() );
                InfoHash infoHash = createTorrentFile( sandboxFilePath, m_sandboxRootPath, torrentFile );
                size_t fileSize = std::filesystem::file_size( sandboxFilePath );

                // add file in resultFsTree
                m_sandboxFsTree.addFile( fs::path(action.m_param2).parent_path(),
                                       sandboxFilePath.filename(),
                                       infoHash,
                                       fileSize );

                // remember files to be removed
                if ( fs::exists( m_driveFolder / action.m_param2 ) )
                {
                    m_toBeRemovedEntries.insert( action.m_param2 );
                }
                break;
            }
            //
            // New folder
            //
            case action_list_id::new_folder: {
                fs::path path = m_driveFolder / action.m_param1;

                // Check that entry is free
                if ( m_sandboxFsTree.findChild( action.m_param1 ) != nullptr )
                {
                    action.m_isInvalid = true;
                    break;
                }

                m_sandboxFsTree.addFolder( action.m_param2 );
                break;
            }
            //
            // Move
            //
            case action_list_id::move: {

                auto* srcChild = m_sandboxFsTree.getEntryPtr( action.m_param1 );
                //m_sandboxFsTree.dbgPrint();

                // Check that src child exists
                if ( srcChild == nullptr )
                {
                    LOG( "invalid 'move' action: src not exists (in FsTree): " << action.m_param1  );
                    action.m_isInvalid = true;
                    break;
                }

                if ( action.m_param1 == action.m_param2 )
                {
                    // nothing to do
                    action.m_isInvalid = true;
                    break;
                }

                fs::path srcPath = m_driveFolder / action.m_param1;
                fs::path destPath = m_driveFolder / action.m_param2;
                fs::path srcInSandboxPath = m_sandboxDriveFolder / action.m_param1;
                fs::path destInSandboxPath = m_sandboxDriveFolder / action.m_param2;

                // Check topology (nesting)
                if ( isPathInsideFolder( destInSandboxPath, srcInSandboxPath ) )
                {
                    LOG( "invalid 'move' action: 'srcPath' is a directory which is an ancestor of 'destPath': " << action.m_param1  );
                    LOG( "invalid 'move' action: srcPath : " << srcPath  );
                    LOG( "invalid 'move' action: destPath : " << destInSandboxPath  );
                    action.m_isInvalid = true;
                    break;
                }

                if ( !fs::exists(srcInSandboxPath) &&
                    (!fs::exists(srcPath) || m_toBeRemovedEntries.contains(action.m_param1)) )
                {
                    LOG( "invalid 'move' action: internal error: src not exists: " << action.m_param1  );
                    action.m_isInvalid = true;
                    break;
                }

                // remember destPath to be removed
                if ( fs::exists(destPath) )
                    m_toBeRemovedEntries.insert( action.m_param2 );

                // remove dest path in sanbox
                if ( fs::exists(destInSandboxPath) )
                    fs::remove_all(destInSandboxPath);

                // prepare parent folder
                fs::create_directories( destInSandboxPath.parent_path() );

                // Move file/folder from drive location
                //
                if ( fs::exists(srcInSandboxPath) && ! m_toBeRemovedEntries.contains(action.m_param1) )
                {
                    // remember srcPath to be removed
                    m_toBeRemovedEntries.insert( action.m_param1 );

                    if ( fs::exists(destInSandboxPath) )
                    {
                        fs::remove_all(destInSandboxPath);
                    }

                    // copy file to dest sandbox
                    fs::create_directories( destInSandboxPath.parent_path() );
                    fs::copy( srcPath, destInSandboxPath );
                }

                // Move file/folder from sanbox location
                if ( fs::exists(srcInSandboxPath) )
                {
                    // move folder/files to dest sandbox
                    if ( fs::is_directory(srcInSandboxPath) )
                    {
                        moveFiles( srcInSandboxPath, destInSandboxPath );
                    }
                    else
                    {
                        fs::remove( destInSandboxPath );
                        fs::copy( srcInSandboxPath, destInSandboxPath );
                    }

                    // remove old torrentfile
                    fs::path oldTorrentFile = m_sandboxTorrentFolder / action.m_param1;
                    if ( fs::exists(oldTorrentFile) )
                        fs::remove_all(oldTorrentFile);
                }
                
                // remove src in sandbox
                fs::remove( srcInSandboxPath );
                fs::remove( m_sandboxTorrentFolder / action.m_param1 );

                // Process torret files
                fs::path destTorrentPath = m_sandboxTorrentFolder / action.m_param2;

                // remove old torrentfile/s
                if ( fs::exists(destTorrentPath) )
                    fs::remove_all(destTorrentPath);
                
                if ( fs::is_directory(destInSandboxPath) )
                {
                    // modify FsTree
                    m_sandboxFsTree.move( action.m_param1, action.m_param2 );
                    Folder* fsTreeFolder = m_sandboxFsTree.getFolderPtr( action.m_param2 );
                    if ( fsTreeFolder == nullptr )
                    {
                        m_modifyHandler( modify_status::failed, InfoHash(), "internal error: fsTreeFolder == nullptr" );
                        return;
                    }
                    fs::create_directories( destTorrentPath );
                    calculateTorrentsForFolderFiles( destInSandboxPath, destTorrentPath, *fsTreeFolder );
                }
                else
                {
                    // create new torrent file (it depends on filename)
                    fs::create_directories( destTorrentPath.parent_path() );
                    InfoHash infoHash = createTorrentFile( destInSandboxPath, m_sandboxRootPath, destTorrentPath );

                    // modify FsTree
                    m_sandboxFsTree.move( action.m_param1, action.m_param2, &infoHash );
                }

                // remember destPath to be removed
                if ( fs::exists( m_driveFolder / action.m_param2 ) )
                {
                    m_toBeRemovedEntries.insert( action.m_param2 );
                }

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

                //LOG( "remove:" << action.m_param1 );
                m_sandboxFsTree.remove( action.m_param1 );

                // remember path to be removed
                if ( fs::exists( m_driveFolder / action.m_param1 ) )
                {
                    m_toBeRemovedEntries.insert( action.m_param1 );
                }
                break;
            }

            } // end of switch()
        } // end of for( const Action& action : m_actionList )

        // calculate new rootHash
        m_sandboxFsTree.doSerialize( m_sandboxFsTreeFile );
        m_resultRootHash = createTorrentFile( m_sandboxFsTreeFile, m_sandboxRootPath, m_sandboxFsTreeTorrent );

        // Call update handler
        m_modifyHandler( modify_status::sandbox_root_hash, m_resultRootHash, "" );

        // start drive update
        updateDrive_1();
    }
    
    void calculateTorrentsForFolderFiles( fs::path srcFolder, fs::path /*torrentFolder*/, Folder& fsTreeFolder )
    {
        for (const auto& entry: fs::directory_iterator(srcFolder)) {

            const auto entryName = entry.path().filename().string();

            if ( entry.is_directory() )
            {
                fs::path subfolder = srcFolder/entryName;
                fs::path torrentSubfolder = srcFolder/entryName;
                auto* child = fsTreeFolder.findChild( entryName );

                // calulate subfolder
                calculateTorrentsForFolderFiles( subfolder, torrentSubfolder, getFolder(*child) );
            }
            else
            {
                fs::path filename = srcFolder/entryName;
                fs::path torrentFilename = srcFolder/entryName;

                // calulate torrent info
                InfoHash infoHash = createTorrentFile( filename, m_sandboxRootPath, torrentFilename );

                // update hash in FsTree
                auto* child = fsTreeFolder.findChild( entryName );
                getFile(*child).m_hash = infoHash;
            }
        }

    }


    // updates drive (1st step) - remove torrents
    // - removes all moved or removed torents
    // - removes current fsTree torrent
    //
    void updateDrive_1()
    {
        // Remove torrents
        for( const Action& action : m_actionList )
        {
            if (action.m_isInvalid)
                continue;

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
        m_session->removeTorrentFromSession( m_fsTreeLtHandle, [this]
        {
            fs::remove( m_fsTreeFile );
            fs::remove( m_fsTreeTorrent );

            updateDrive_2();
        });
    }

    // updates drive (2st phase)
    // - move new files from sandbox to drive
    // - move new torrents from sandbox to drive
    // - add new torrents to session
    //
    void updateDrive_2()
    {
        //
        // Remove files
        //
        for( const std::string& path : m_toBeRemovedEntries )
        {
            LOG( "m_toBeRemovedEntries remove:" << path );
            fs::remove_all( m_driveFolder / path );
            fs::remove_all( m_torrentFolder / path );
        }

        m_toBeRemovedEntries.clear();

        //
        // Move files from sandbox to drive folder
        //
        m_rootHash = m_resultRootHash;
        if ( fs::exists(m_sandboxDriveFolder) )
            moveFiles( m_sandboxDriveFolder, m_driveFolder );
        if ( fs::exists(m_sandboxTorrentFolder) )
            moveFiles( m_sandboxTorrentFolder, m_torrentFolder );

        fs::rename( m_sandboxFsTreeFile, m_fsTreeFile );

        fs::rename( m_sandboxFsTreeTorrent, m_fsTreeTorrent );

        fs::remove_all( m_sandboxRootPath );

        //
        // Add torrents to session
        //
        for( const Action& action : m_actionList )
        {
            if (action.m_isInvalid)
                continue;

            switch( action.m_actionId )
            {
                case action_list_id::upload:
                case action_list_id::move: {
    
                    const auto* child = m_sandboxFsTree.findChild( fs::path( action.m_param2 ) );
    
                    if ( child != nullptr && !isFolder(*child) )
                    {
                        const File& fsTreeFile = getFile(*child);
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

        // Add FsTree torrent to session
        m_fsTreeLtHandle = m_session->addTorrentFileToSession( m_fsTreeTorrent, m_fsTreeTorrent.parent_path(), m_otherReplicators );

        // Call update handler
        m_modifyHandler( modify_status::update_completed, InfoHash(), "" );
    }

    static void moveFiles( fs::path srcFolder, fs::path destFolder )
    {
        for (const auto& entry: fs::directory_iterator(srcFolder)) {

            const auto entryName = entry.path().filename().string();

            if ( entry.is_directory() )
            {
                fs::path destSubfolder = destFolder/entryName;
                fs::create_directory( destSubfolder );

                moveFiles( srcFolder/entryName, destSubfolder );
            }
            else
            {
                //LOG( "fs::rename: " << srcFolder/entryName << " to " << destFolder/entryName );
                fs::rename( srcFolder/entryName, destFolder/entryName );
            }
        }

    }

    static void errorHandler( Session*, libtorrent::alert* alert )
    {
        if ( alert->type() == lt::listen_failed_alert::alert_type )
        {
            std::cerr << "replicator socket error: " << alert->message() << std::endl << std::flush;
            exit(-1);
        }
    }
};


std::shared_ptr<Drive> createDefaultDrive(
        std::shared_ptr<Session> session,
        const std::string&       replicatorRootFolder,
        const std::string&       replicatorSandboxRootFolder,
        const std::string&       drivePubKey,
        size_t                   maxSize,
        const endpoint_list&     otherReplicators )
{
    return std::make_shared<DefaultDrive>( session,
                                           replicatorRootFolder,
                                           replicatorSandboxRootFolder,
                                           drivePubKey,
                                           maxSize,
                                           otherReplicators );
}

}}

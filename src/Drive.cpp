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

namespace fs = std::filesystem;

namespace xpx_storage_sdk {
using namespace fs_tree;

// DefaultDrive
class DefaultDrive: public Drive {

    using LtSession = std::shared_ptr<LibTorrentSession>;

    LtSession     m_distributionSession;
    LtSession     m_modifyDriveSession;

    std::string   m_listenInterface;
    endpoint_list m_otherReplicators;

    fs::path      m_rootPath;
    size_t        m_maxSize;

    InfoHash      m_rootDriveHash;

    fs::path      m_driveFolder;
    fs::path      m_torrentFolder;
    fs::path      m_fsTreeFile;
    fs::path      m_fsTreeTorrentFile;

    fs::path      m_sandboxFolder;
    fs::path      m_sandboxDriveFolder;
    fs::path      m_sandboxTorrentFolder;
    fs::path      m_sandboxFsTreeFile;
    fs::path      m_sandboxActionListFile;

    InfoHash      m_modifyDataInfoHash;
    ModifyDriveResultHandler m_resultHandler;

    ActionList    m_actionList;
    FsTree        m_resultFsTree;

public:

    DefaultDrive( std::string listenInterface, std::string rootPath, size_t maxSize, endpoint_list otherReplicators )
        : m_listenInterface(listenInterface),
          m_rootPath(rootPath),
          m_maxSize(maxSize),
          m_otherReplicators(otherReplicators)
    {
        init();
    }

    virtual ~DefaultDrive() {}

    // init
    //
    void init()
    {
        m_driveFolder       = fs::path( m_rootPath ) / "drive";
        m_torrentFolder     = fs::path( m_rootPath ) / "torrent";
        m_fsTreeFile        = fs::path( m_rootPath ) / "FsTree.bin";
        m_fsTreeTorrentFile = fs::path( m_rootPath ) / "FsTree.torrent";

        m_sandboxFolder         = fs::path( m_rootPath ) / "tmp" / "sandbox";
        m_sandboxDriveFolder    = fs::path( m_rootPath ) / "tmp" / "sandbox" / "drive";
        m_sandboxTorrentFolder  = fs::path( m_rootPath ) / "tmp" / "sandbox" / "torrent";
        m_sandboxFsTreeFile     = fs::path( m_rootPath ) / "tmp" / "sandbox" / "FsTree.bin";
        m_sandboxActionListFile = fs::path( m_rootPath ) / "tmp" / "sandbox" / "ActionList.bin";

        if ( !fs::exists( m_fsTreeTorrentFile ) ) {
            updateFsTreeTorrent();
        }

        startDistributionSession();
    }

    // Start libtorrent session with 'torrent files'
    //
    void startDistributionSession()
    {
        m_distributionSession = createDefaultLibTorrentSession( m_listenInterface );

        // Add fsTreeFile
        m_distributionSession->addTorrentFileToSession( m_fsTreeTorrentFile, m_rootPath, m_otherReplicators );

        FsTree fsTree;
        fsTree.deserialize( m_fsTreeFile );
        addFilesToDistributionSession( m_driveFolder, m_torrentFolder, fsTree );
    }

    // addFilesToDistributionSession
    //
    void addFilesToDistributionSession( fs::path folderPath, fs::path torrentFolderPath, fs_tree::Folder fsTreeFolder )
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
                addFilesToDistributionSession( folderPath / name,
                                               torrentFolderPath / name,
                                               fsTreeFolder.getSubfolderOrCreate( name ) );
            }
            else if ( child.is_regular_file() )
            {
                // Add file
                //
                fs::path torrentFile = torrentFolderPath / name;

                // Get FsTree child
                fs_tree::Folder::Child* fsTreeChild = fsTreeFolder.findChild( name );

                // Throw error if it's folder
                if ( fsTreeChild != nullptr && isFolder(*fsTreeChild) ) {
                    throw std::runtime_error( std::string("attempt to create a file with existing folder with same name: ") + name.string() );
                }

                // Calculate torrent info
                InfoHash fileHash;
                if ( !fs::exists( torrentFile ) || fsTreeChild == nullptr ) {
                    fileHash = createTorrentFile( child.path(), torrentFile );
                }

                // Add file into FsTree
                if ( fsTreeChild == nullptr ) {
                    size_t fileSize = std::filesystem::file_size( child.path() );
                    fsTreeFolder.m_childs.emplace_back( fs_tree::File{name,fileHash,fileSize} );
                }

                m_distributionSession->addTorrentFileToSession( torrentFile, child.path() );
            }
        }
    }

    // updateFsTreeTorrent
    //
    void updateFsTreeTorrent()
    {
        if ( !fs::exists( m_driveFolder) ) {
            fs::create_directories( m_driveFolder );
        }

        if ( !fs::exists( m_torrentFolder) ) {
            fs::create_directories( m_torrentFolder );
        }

        if ( !fs::exists( m_fsTreeFile ) ) {
            // Create empty FsTree
            FsTree().doSerialize( m_fsTreeFile );
        }

        // Calculate fsTree torrent file and root hash
        m_rootDriveHash = createTorrentFile( m_fsTreeFile, m_fsTreeTorrentFile );
    }

    // recalculateHashes
    //
    void recalculateHashes()
    {
        fs::remove_all( m_torrentFolder );
        fs::remove_all( m_fsTreeTorrentFile );
        fs::remove_all( m_fsTreeFile );
        updateFsTreeTorrent();
    }

    void startModifyDrive( InfoHash modifyDataInfoHash, ModifyDriveResultHandler resultHandler ) override
    {
        using namespace std::placeholders;  // for _1, _2, _3

        m_modifyDataInfoHash = modifyDataInfoHash;
        m_resultHandler      = resultHandler;

        m_distributionSession->downloadFile( modifyDataInfoHash,
                                             m_sandboxFolder.parent_path(),
                                             std::bind( &DefaultDrive::downloadHandler, this, _1, _2, _3 ),
                                             m_otherReplicators );
    }

    void downloadHandler( download_status::code code, InfoHash infoHash, std::string info )
    {
        if ( m_modifyDataInfoHash != infoHash )
        {
            m_resultHandler( false, InfoHash(), std::string("DefaultDrive::downloadHandler: internal error: ") + info );
            return;
        }

        if ( code == download_status::failed )
        {
            m_resultHandler( false, InfoHash(), std::string("modify drive: download failed: ") + info );
            return;
        }

        if ( code == download_status::complete )
        {
            modifyDrive();
        }
    }

    void modifyDrive()
    {
        if ( !fs::exists( m_sandboxActionListFile ) )
        {
            LOG( "m_sandboxActionListFile=" << m_sandboxActionListFile );
            m_resultHandler( false, InfoHash(), "modify drive: 'ActionList.bin' is absent: " );
            return;
        }

        ActionList m_actionList;
        m_actionList.deserialize( m_sandboxActionListFile );

        FsTree m_resultFsTree;
        m_resultFsTree.deserialize( m_fsTreeFile );

        for( const Action& action : m_actionList )
        {
            switch( action.m_actionId )
            {
            case action_list_id::upload: {
                // calculate paths
                fs::path file = m_sandboxDriveFolder / action.m_param2;
                LOG( "upload file:   " << file );
                fs::path torrentFile = m_sandboxTorrentFolder / action.m_param2;
                LOG( "upload torrent:" << torrentFile );

                // calculate torrent, hash, and size
                InfoHash infoHash = createTorrentFile( file, torrentFile );
                size_t fileSize = std::filesystem::file_size( file );
                
                // add file in resultFsTree
                fs::create_directories( torrentFile.parent_path() );
                m_resultFsTree.addFile( fs::path(action.m_param1).parent_path(),
                                       file.filename(),
                                       infoHash,
                                       fileSize );
                return;
            }
            case action_list_id::new_folder:
                m_resultFsTree.addFolder( action.m_param1 );
                break;
            case action_list_id::rename:
                m_resultFsTree.move( action.m_param1, action.m_param2 );
                break;
            case action_list_id::remove:
                m_resultFsTree.remove( action.m_param1 );
                break;
            case action_list_id::none:
                break;
            }
        }
    }

    //    void executeActionList( InfoHash actionListHash ) override {

//        // clear sandbox
////        fs::remove_all( m_sandboxFolder );
////        fs::create_directories( m_sandboxFolder );

////        m_newFsTree.deserialize( m_fsTreeFile );

//        // start upload of the action list
////        m_fileTransmitter->download( actionListHash, m_tmpDrivePath.string(),
////                                     std::bind( &DefaultDrive::handleActionList, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3 ) );
//    }

//    void handleActionList( download_status::code code, InfoHash, std::string fileName ) {

//        if ( code == download_status::failed ) {
//            //TODO cancel "modify drive"
//            return;
//        }

//        if ( code == download_status::complete ) {
//            //TODO check action list hash
//            m_actionList.deserialize( "m_tmpActionListFile.string()" );
//            m_currentActionIndex = 0;
//            exectuteAction();
//        }

//    }

    void exectuteAction() {

//        for( ; m_currentActionIndex < m_actionList.size(); m_currentActionIndex++ )
//        {
//            //TODO const
//            Action& action = m_actionList[m_currentActionIndex];
//            switch( action.m_actionId )
//            {
//            case action_list_id::upload: {
//                //fs::path filePath = action.m_param1;
//                //TODO
//                std::string outputFolder = "???";
//                m_fileTransmitter->download( action.m_hash, outputFolder, std::bind( &DefaultDrive::handleUnploadFile, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3 ) );
//                return;
//            }
//            case action_list_id::new_folder:
//                m_newFsTree.addFolder( action.m_param1 );
//                break;
//            case action_list_id::rename:
//                m_newFsTree.move( action.m_param1, action.m_param2 );
//                break;
//            case action_list_id::remove:
//                m_newFsTree.remove( action.m_param1 );
//                break;
//            case action_list_id::none:
//                break;
//            }
//        }
        //TODO
    }

};


std::shared_ptr<Drive> createDefaultDrive(
        std::string listenInterface,
        std::string rootPath,
        size_t maxSize,
        endpoint_list otherReplicators)
{
    return std::make_shared<DefaultDrive>( listenInterface, rootPath, maxSize, otherReplicators );
}

}

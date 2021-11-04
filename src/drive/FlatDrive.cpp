/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

/*
 add DriveId to torrent?
 
 -----------------------------------
 Modify
 -----------------------------------
 
 onSandboxCalculated()   -> sendMyPercentsToExtension() --
                                                         --> sendApprovalTransaction() or sendSingleApprovalTransaction()
 onPercents()            -> sendPercentsToExtension()   --
 
 onApprovalTransaction() -> move-sandbox-to-drive
 
 onCancel()              -> cancel
 
 -----------------------------------
 Download Channel
 -----------------------------------

 getReceipt()
 
 //onSingleApprovalTransaction() -> nothing to do
 
 */

#include "drive/FlatDrive.h"
#include "drive/Replicator.h"
#include "drive/Session.h"
#include "drive/ActionList.h"
#include "drive/Utils.h"
#include "drive/FsTree.h"
#include "drive/log.h"

#include <cereal/types/vector.hpp>
#include <cereal/types/array.hpp>
#include <cereal/archives/portable_binary.hpp>

#include <filesystem>
#include <set>
#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include <shared_mutex>

#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>


namespace fs = std::filesystem;

namespace sirius::drive {

//
// DrivePaths - drive paths, used at replicator side
//
class FlatDrivePaths {
protected:
    FlatDrivePaths( const std::string&  replicatorRootFolder,
                const std::string&      replicatorSandboxRootFolder,
                const Key&              drivePubKey )
        :
          m_drivePubKey( drivePubKey ),
          m_replicatorRoot( replicatorRootFolder ),
          m_replicatorSandboxRoot( replicatorSandboxRootFolder )
    {}

    virtual ~FlatDrivePaths() {}

protected:
    const Key       m_drivePubKey;

    const fs::path  m_replicatorRoot;
    const fs::path  m_replicatorSandboxRoot;

    // Drive paths
    const fs::path  m_driveRootPath     = m_replicatorRoot / arrayToString(m_drivePubKey.array());
    const fs::path  m_driveFolder       = m_driveRootPath  / "drive";
    const fs::path  m_torrentFolder     = m_driveRootPath  / "torrent";
    const fs::path  m_fsTreeFile        = m_driveRootPath  / "fs_tree" / FS_TREE_FILE_NAME;
    const fs::path  m_fsTreeTorrent     = m_driveRootPath  / "fs_tree" / FS_TREE_FILE_NAME ".torrent";

    // Sandbox paths
    const fs::path  m_sandboxRootPath       = m_replicatorSandboxRoot / arrayToString(m_drivePubKey.array());
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

    // UseTorrentInfo is used to avoid adding torrents into session with the same hash
    // and for deleting unused files and torrents from session
    struct UseTorrentInfo {
        lt_handle m_ltHandle = {};
        bool      m_isUsed = true;
    };

    LtSession     m_session;

    size_t        m_maxSize;
    
    // It has the following statuses: "modification started", "sandbox calculated", modification approved"
    std::shared_mutex m_mutex;
    bool m_sandboxCalculated          = false;
    bool m_approveTransactionReceived = false;
    bool m_approveTransactionSent     = false; // approval transaction has been sent
    
    bool m_driveIsClosing             = false;

    // It is needed if a new 'modifyRequest' is received, but drive is syncing with sandbox
    std::deque<ModifyRequest> m_modifyRequestQueue;

    // Client data (for drive modification)
    std::optional<ModifyRequest> m_modifyRequest;
    lt_handle                    m_modifyDataLtHandle; // used for removing torrent from session
    bool m_modificationIsCanceling = false;

    // FsTree
    FsTree        m_fsTree;
    FsTree        m_sandboxFsTree;
    lt_handle     m_fsTreeLtHandle; // used for removing FsTree torrent from session

    // Root hashes
    InfoHash      m_rootHash;
    InfoHash      m_sandboxRootHash;

    // List of replicators that support this drive
    ReplicatorList    m_replicatorList;
    
    // opinion
    std::optional<ApprovalTransactionInfo>  m_myOpinion;

    // opinions from other replicators
    // (key is a replicator key, one replicator one opinion)
    std::map<std::array<uint8_t,32>,ApprovalTransactionInfo>    m_otherOpinions;
    
    //todo may be they are outstripping opinions
    //std::vector<ApprovalTransactionInfo>    m_unknownOpinions;
    
    std::optional<boost::asio::high_resolution_timer> m_opinionTimer;

    // Will be called at the end of the sanbox work
    ReplicatorEventHandler& m_eventHandler;
    
    // It is as 1-st parameter in functions of ReplicatorEventHandler (for debugging)
    Replicator&             m_replicator;

    // TorrentHandleMap is used to avoid adding torrents into session with the same hash
    // and for deleting unused files and torrents from session
    std::map<InfoHash,UseTorrentInfo> m_torrentHandleMap;

public:

    DefaultFlatDrive(
                  std::shared_ptr<Session>  session,
                  const std::string&        replicatorRootFolder,
                  const std::string&        replicatorSandboxRootFolder,
                  const Key&                drivePubKey,
                  size_t                    maxSize,
                  ReplicatorEventHandler&   eventHandler,
                  Replicator&               replicator,
                  const ReplicatorList&     replicatorList )
        :
          FlatDrivePaths( replicatorRootFolder, replicatorSandboxRootFolder, drivePubKey ),
          m_session(session),
          m_maxSize(maxSize),
          m_replicatorList(replicatorList),
          m_eventHandler(eventHandler),
          m_replicator(replicator)
    {
        // Initialize drive
        init();
    }

    virtual ~DefaultFlatDrive() {
        terminate();
    }

    const Key& drivePublicKey() const override { return m_drivePubKey; }

    void terminate() {
        //TODO?
    }

    uint64_t maxSize() const override {
        return m_maxSize;
    }

    InfoHash rootHash() const override {
        return m_rootHash;
    }
    
    InfoHash sandboxRootHash() const override {
        return m_sandboxRootHash;
    }

    ReplicatorList getReplicators() override {
        std::scoped_lock<std::shared_mutex> lock(m_mutex);
        return m_replicatorList;
    }

    void updateReplicators(const ReplicatorList& replicators) override {
        if (replicators.empty()) {
            LOG_ERR( "ReplicatorList is empty!");
            return;
        }

        std::scoped_lock<std::shared_mutex> lock(m_mutex);
        for (const ReplicatorInfo& ri : replicators) {
            const auto& r = std::find(m_replicatorList.begin(), m_replicatorList.end(), ri);
            if(r != m_replicatorList.end()) {
                *r = ri;
            } else {
                m_replicatorList.push_back(ri);
            }
        }
    }
    
    uint64_t sandboxFsTreeSize() const override {
        return fs::file_size( m_sandboxFsTreeFile );
    }

    void getSandboxDriveSizes( uint64_t& metaFilesSize, uint64_t& driveSize ) const override
    {
        metaFilesSize = fs::file_size( m_sandboxFsTreeTorrent);
        driveSize = 0;
        m_sandboxFsTree.getSizes( m_driveFolder, m_torrentFolder, metaFilesSize, driveSize );
        driveSize += metaFilesSize;
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
        m_rootHash = createTorrentFile( m_fsTreeFile, m_fsTreeFile.parent_path(), m_fsTreeTorrent );

        //TODO compare rootHash with blockchain?

        // Add files to session
        addFilesToSession( m_driveFolder, m_torrentFolder, m_fsTree );

        // Add FsTree to session
        m_fsTreeLtHandle = m_session->addTorrentFileToSession( m_fsTreeTorrent,
                                                               m_fsTreeTorrent.parent_path(),
                                                               lt::sf_is_replicator );
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
                
                //fs::path torrentFile = m_torrentFolder / hashToFileName(  );
                auto torrentHandle = m_session->addTorrentFileToSession( torrentFile, m_driveFolder, lt::sf_is_replicator, {} );
                m_torrentHandleMap.try_emplace( getFile(*fsTreeChild).hash(), UseTorrentInfo{ torrentHandle, true } );
            }
        }
    }
    
    void cancelModifyDrive( const Hash256& transactionHash ) override
    {
        if ( m_modifyRequest && !(transactionHash == m_modifyRequest->m_transactionHash) )
        {
            LOG_ERR( "cancelModifyDrive(): invalid transactionHash: " << transactionHash );
            return;
        }
        //TODO
    }

    void startDriveClosing( const Hash256& transactionHash ) override
    {
        m_driveIsClosing = true;
        
        std::shared_lock<std::shared_mutex> lock(m_mutex);

        if ( m_modifyRequest )
        {
            m_session->removeTorrentsFromSession( {m_modifyDataLtHandle}, [this,transactionHash](){
                continueDriveClosing( transactionHash );
            });
        }
    }

    void continueDriveClosing( const Hash256& transactionHash )
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);

        std::set<lt_handle> tobeRemovedTorrents;

        for( auto& [key,value]: m_torrentHandleMap )
        {
            if ( value.m_isUsed )
                tobeRemovedTorrents.insert( value.m_ltHandle );
        }

        m_session->removeTorrentsFromSession( std::move(tobeRemovedTorrents), [this,transactionHash](){
            m_replicator.closeDriveChannels( transactionHash, *this );
        });
    }

    void synchronizeDriveWithSandbox()
    {
        assert( m_sandboxCalculated );
        
        // complete drive update
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);

            if ( !m_modifyRequest )
            {
                LOG_ERR( "approveDriveModification(): modification is not started" )
            }
            else
            {
                if ( m_sandboxCalculated )
                {
                    lock.unlock();
                    updateDrive_1();
                }
            }
        }
    }


    // startModifyDrive - should be called after client 'modify request'
    //
    void startModifyDrive( ModifyRequest&& modifyRequest ) override
    {
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            
            m_replicatorList = modifyRequest.m_replicatorList;

            if ( m_modifyRequest )
            {
                //LOG_ERR( "startModifyDrive():: prevoius modification is not completed" );
                m_modifyRequestQueue.emplace_back( std::move(modifyRequest) );
                return;
            }

            m_sandboxCalculated          = false;
            m_approveTransactionReceived = false;
            m_approveTransactionSent     = false;

            // remove old opinions
            m_otherOpinions.clear();
        }
        
        // remove my opinion
        m_myOpinion.reset();

        m_modifyRequest = std::move( modifyRequest );

        // clear client session folder
        fs::remove_all( m_sandboxRootPath );
        fs::create_directories( m_sandboxRootPath);

        using namespace std::placeholders;  // for _1, _2, _3

        m_modifyDataLtHandle = m_session->download( DownloadContext(
                                            DownloadContext::client_data,
                                            std::bind( &DefaultFlatDrive::downloadHandler, this, _1, _2, _3, _4, _5, _6 ),
                                            modifyRequest.m_clientDataInfoHash,
                                            modifyRequest.m_transactionHash,
                                            0, //todo
                                            ""),
                                        m_sandboxRootPath,
                                        modifyRequest.m_replicatorList );
    }

    // will be called by Session
    void downloadHandler( download_status::code code,
                          const InfoHash& infoHash,
                          const std::filesystem::path /*filePath*/,
                          size_t /*downloaded*/,
                          size_t /*fileSize*/,
                          const std::string& errorText )
    {
        if ( !m_modifyRequest )
        {
            m_eventHandler.modifyTransactionIsCanceled( m_replicator, m_drivePubKey, {}, "DefaultDrive::downloadHandler: internal error", 0 );
            return;
        }

        if ( m_modifyRequest->m_clientDataInfoHash != infoHash )
        {
            m_eventHandler.modifyTransactionIsCanceled( m_replicator, m_drivePubKey, m_modifyRequest->m_transactionHash, "DefaultDrive::downloadHandler: internal error", 0 );
            return;
        }

        if ( code == download_status::failed )
        {
            m_eventHandler.modifyTransactionIsCanceled( m_replicator, m_drivePubKey, m_modifyRequest->m_transactionHash, errorText, 0 );
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
        // Check that client data exist
        if ( !fs::exists(m_clientDataFolder) || !fs::is_directory(m_clientDataFolder) )
        {
            LOG( "m_clientDataFolder=" << m_clientDataFolder );
            m_eventHandler.modifyTransactionIsCanceled( m_replicator, m_drivePubKey, m_modifyRequest->m_transactionHash, "modify drive: 'client-data' is absent", -1 );
            return;
        }

        // Check 'actionList.bin' is received
        if ( !fs::exists( m_clientActionListFile ) )
        {
            LOG( "m_clientActionListFile=" << m_clientActionListFile );
            m_eventHandler.modifyTransactionIsCanceled( m_replicator, m_drivePubKey, m_modifyRequest->m_transactionHash, "modify drive: 'ActionList.bin' is absent", -1 );
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
                InfoHash fileHash = calculateInfoHashAndCreateTorrentFile( clientFile, arrayToString(m_drivePubKey.array()), m_torrentFolder, "" );
                size_t fileSize = std::filesystem::file_size( clientFile );

                // rename file and move it into drive folder
                std::string newFileName = m_driveFolder / hashToFileName( fileHash );
                fs::rename( clientFile, newFileName );

                // add file in resultFsTree
                m_sandboxFsTree.addFile( fs::path(action.m_param2).parent_path(),
                                       clientFile.filename(),
                                       fileHash,
                                       fileSize );

                // add ref into 'torrentMap'
                m_torrentHandleMap.try_emplace( fileHash, UseTorrentInfo{} );

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
                    //m_torrentMap.try_emplace( fileHash, UseTorrentInfo{} );
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
                    m_torrentHandleMap.try_emplace( fileHash, UseTorrentInfo{} );
                } );

                break;
            }

            } // end of switch()
        } // end of for( const Action& action : actionList )

        // calculate new rootHash
        m_sandboxFsTree.doSerialize( m_sandboxFsTreeFile );
        m_sandboxRootHash = createTorrentFile( m_sandboxFsTreeFile, m_sandboxRootPath, m_sandboxFsTreeTorrent );

        myRootHashIsCalculated();

        // start drive update (if 'approveTransaction' is received)
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            
            if ( m_approveTransactionReceived )
            {
                lock.unlock();
                updateDrive_1();
            }
        }
    }
    
    void createMyOpinion()
    {
        auto trafficInfo = m_replicator.getMyDownloadOpinion(  m_modifyRequest->m_transactionHash );

        //
        // Calculate upload opinion
        //
        SingleOpinion opinion( m_replicator.replicatorKey().array() );
        for( const auto& replicatorIt : m_modifyRequest->m_replicatorList )
        {
            // get data size received from 'replicatorIt.m_publicKey'
            if ( auto it = trafficInfo.m_modifyTrafficMap.find( replicatorIt.m_publicKey.array() );
                    it != trafficInfo.m_modifyTrafficMap.end() )
            {
                opinion.m_replicatorUploadBytes.push_back( it->second.m_receivedSize );
            }
            else
            {
                opinion.m_replicatorUploadBytes.push_back( 0 );
            }
            
            auto& v = opinion.m_uploadReplicatorKeys;
            v.insert( v.end(), replicatorIt.m_publicKey.array().begin(), replicatorIt.m_publicKey.array().end() );
        }
        if ( auto it = trafficInfo.m_modifyTrafficMap.find( m_modifyRequest->m_clientPublicKey.array() );
                it != trafficInfo.m_modifyTrafficMap.end() )
        {
            opinion.m_clientUploadBytes = it->second.m_receivedSize;
        }
        opinion.Sign( m_replicator.keyPair(), m_modifyRequest->m_transactionHash, m_sandboxRootHash );

        // Calculate size of torrent files and total drive size
        uint64_t metaFilesSize;
        uint64_t driveSize;
        getSandboxDriveSizes( metaFilesSize, driveSize );

        std::unique_lock<std::shared_mutex> lock(m_mutex);

        m_myOpinion = std::optional<ApprovalTransactionInfo> {{ m_drivePubKey.array(),
                                                                m_modifyRequest->m_transactionHash.array(),
                                                                m_sandboxRootHash.array(),
                                                                sandboxFsTreeSize(),
                                                                metaFilesSize,
                                                                driveSize,
                                                                { std::move(opinion) }}};
    }

#pragma mark --myRootHashIsCalculated--
    void myRootHashIsCalculated()
    {
        // Notify
        m_eventHandler.rootHashIsCalculated( m_replicator, m_drivePubKey, m_modifyRequest->m_transactionHash, m_sandboxRootHash );
        
        // Calculate my opinion
        createMyOpinion();
        
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            
            m_sandboxCalculated = true;
            
            if ( m_approveTransactionReceived )
            {
                lock.unlock();
                sendSingleApprovalTransaction();
            }
            else
            {
                // Send my opinion to other replicators
                shareMyOpinion();
                
                // May be send approval transaction
                checkOpinionNumberAndStartTimer();
            }
        }
    }
    
    void shareMyOpinion()
    {
        std::ostringstream os( std::ios::binary );
        cereal::PortableBinaryOutputArchive archive( os );
        archive( *m_myOpinion );
        
        for( const auto& replicatorIt : m_modifyRequest->m_replicatorList )
        {
            m_replicator.sendMessage( "opinion", replicatorIt.m_endpoint, os.str() );
        }
    }
    
    void checkOpinionNumberAndStartTimer()
    {
        // m_replicatorList is the list of other replicators (it does not contain our replicator)
        auto replicatorNumber = m_modifyRequest->m_replicatorList.size()+1;

        // check opinion number
        if ( m_myOpinion && m_otherOpinions.size() >= ((replicatorNumber)*2)/3
            && !m_approveTransactionSent && !m_approveTransactionReceived )
        {
            // start timer if it is not started
            if ( !m_opinionTimer )
                m_opinionTimer = m_session->startTimer( m_replicator.getModifyApprovalTransactionTimerDelay(),
                                    [this]() { opinionTimerExpired(); } );
        }
    }
    
    // updates drive (1st step after aprove)
    // - remove torrents from session
    //
    void updateDrive_1()
    {
        _LOG( "updateDrive_1:" << m_replicator.dbgReplicatorName() );
        
        // Prepare map for detecting of used files
        for( auto& it : m_torrentHandleMap )
            it.second.m_isUsed = false;

        // Mark used files
        markUsedFiles( m_sandboxFsTree );

        // Prepare set<> for to be removed torrents
        std::set<lt::torrent_handle> toBeRemovedTorrents;

        // Add unused files into set<>
        for( const auto& it : m_torrentHandleMap )
        {
            const UseTorrentInfo& info = it.second;
            if ( !info.m_isUsed )
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
            const UseTorrentInfo& info = it.second;
            if ( !info.m_isUsed )
            {
                const auto& hash = it.first;
                std::string filename = hashToFileName( hash );
                fs::remove( fs::path(m_driveFolder) / filename );
                fs::remove( fs::path(m_torrentFolder) / filename );
                LOG("+++ updateDrive_2: removed: " << filename );
            }
        }

        // remove unused data from 'fileMap'
        std::erase_if( m_torrentHandleMap, [] (const auto& it) { return !it.second.m_isUsed; } );

        //
        // Add torrents into session
        //
        for( auto& it : m_torrentHandleMap )
        {
            // load torrent (if it is not loaded)
            if ( !it.second.m_ltHandle.is_valid() )
            {
                std::string fileName = hashToFileName( it.first );
                it.second.m_ltHandle = m_session->addTorrentFileToSession( m_torrentFolder / fileName,
                                                                           m_driveFolder,
                                                                           lt::sf_is_replicator );
            }
        }

        // Add FsTree torrent to session
        m_fsTreeLtHandle = m_session->addTorrentFileToSession( m_fsTreeTorrent,
                                                               m_fsTreeTorrent.parent_path(),
                                                               lt::sf_is_replicator );

        // Call update handler
        m_eventHandler.driveModificationIsCompleted( m_replicator, m_drivePubKey, m_modifyRequest->m_transactionHash, m_rootHash );

        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);

            m_modifyRequest.reset();

            if ( !m_modifyRequestQueue.empty() )
            {
                auto request = std::move( m_modifyRequestQueue.front() );
                m_modifyRequestQueue.pop_front();
                lock.unlock();
                startModifyDrive( std::move(request) );
            }
        }
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
                    it->second.m_isUsed = true;
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
    
//    void     onDownloadOpinionReceived( const DownloadApprovalTransactionInfo& anOpinion ) override
//    {
//        //todo
//    }
//
//    void     prepareDownloadApprovalTransactionInfo() override
//    {
//        //todo
//    }
    
    // todo (could be removed?)
//    const ModifyRequest& modifyRequest() const override
//    {
//        return *m_modifyRequest;
//    }
    
    virtual void onOpinionReceived( const ApprovalTransactionInfo& anOpinion ) override
    {
        if ( anOpinion.m_opinions.size() != 1 )
            return; //is it spam?
        
        auto& replicatorKey = anOpinion.m_opinions[0].m_replicatorKey;
        
        // check public key
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            auto count = std::count_if( m_replicatorList.begin(), m_replicatorList.end(),
                                        [&replicatorKey] (const auto& r){
                                            return r.m_publicKey == replicatorKey;} );
            //todo unknown replicator (or spam)
            if ( count != 1 )
                return;
        }
        
        // verify sign
        if ( !anOpinion.m_opinions[0].Verify( anOpinion.m_modifyTransactionHash, anOpinion.m_rootHash ) )
        {
            // invalid ApprovalTransactionInfo
            //todo
            return;
        }

        std::unique_lock<std::shared_mutex> lock(m_mutex);

        if ( !m_modifyRequest || anOpinion.m_modifyTransactionHash != m_modifyRequest->m_transactionHash.array())
        {
            // it seems that our drive is significantly behind
            // todo remove old opinions from this replicator
            //todo m_unknownOpinions.push_back( anOpinion );
            return;
        }
        
        // todo verify transaction, duplicates ...

        // May be send approval transaction
        m_otherOpinions[replicatorKey] = anOpinion;
        checkOpinionNumberAndStartTimer();
    }
    
    void opinionTimerExpired()
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);

        if ( m_approveTransactionSent || m_approveTransactionReceived || m_approveTransactionReceived )
            return;
        
        ApprovalTransactionInfo info = {    m_drivePubKey.array(),
                                            m_myOpinion->m_modifyTransactionHash,
                                            m_myOpinion->m_rootHash,
                                            m_myOpinion->m_fsTreeFileSize,
                                            m_myOpinion->m_metaFilesSize,
                                            m_myOpinion->m_driveSize,
                                            {}};
        
        info.m_opinions.reserve( m_otherOpinions.size()+1 );
        info.m_opinions.emplace_back(  m_myOpinion->m_opinions[0] );
        for( const auto& otherOpinion : m_otherOpinions ) {
            info.m_opinions.emplace_back( otherOpinion.second.m_opinions[0] );
        }
        
        // notify event handler
        m_eventHandler.modifyApprovalTransactionIsReady( m_replicator, std::move(info) );
        
        m_approveTransactionSent = true;
    }

    virtual void onApprovalTransactionHasBeenPublished( const ApprovalTransactionInfo& transaction ) override
    {
        if ( !m_modifyRequest || m_modifyRequest->m_transactionHash != transaction.m_modifyTransactionHash )
        {
            //TODO
            assert(0);
        }
        
        // stop timer
        m_opinionTimer.reset();
        
        m_approveTransactionReceived = true;
        
        if ( !m_sandboxCalculated )
        {
            // wait root hash
            return;
        }
        else
        {
            const auto& v = transaction.m_opinions;
            auto it = std::find_if( v.begin(), v.end(), [this] (const auto& opinion) {
                            return opinion.m_replicatorKey == m_replicator.replicatorKey().array();
            });
            
            // Is my opinion present
            if ( it != v.end() )
            {
                synchronizeDriveWithSandbox();
            }
            else
            {
                // Send Single Aproval Transaction
                if ( m_myOpinion )
                    sendSingleApprovalTransaction();
            }
        }
    }

    void sendSingleApprovalTransaction()
    {
        auto copy = *m_myOpinion;
        m_eventHandler.singleModifyApprovalTransactionIsReady( m_replicator, std::move(copy) );
    }

    virtual void onSingleApprovalTransactionHasBeenPublished( const ApprovalTransactionInfo& transaction ) override
    {
        synchronizeDriveWithSandbox();
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
        const Key&               drivePubKey,
        size_t                   maxSize,
        ReplicatorEventHandler&  eventHandler,
        Replicator&              replicator,
        const ReplicatorList&    replicators )

{
    return std::make_shared<DefaultFlatDrive>( session,
                                           replicatorRootFolder,
                                           replicatorSandboxRootFolder,
                                           drivePubKey,
                                           maxSize,
                                           eventHandler,
                                           replicator,
                                           replicators );
}

}

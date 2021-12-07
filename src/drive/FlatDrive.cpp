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
#include "drive/BackgroundExecutor.h"
#include "drive/ActionList.h"
#include "drive/Utils.h"
#include "drive/FsTree.h"
#include "drive/log.h"

#include <cereal/types/vector.hpp>
#include <cereal/types/array.hpp>
#include <cereal/archives/portable_binary.hpp>

#include <filesystem>
#include <set>
#include <functional>
#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include <shared_mutex>

#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>

#include <boost/multiprecision/cpp_int.hpp>
#include <numeric>

namespace fs = std::filesystem;

namespace sirius::drive {

#define DBG_MAIN_THREAD { assert( m_dbgThreadId == std::this_thread::get_id() ); }
#define DBG_SECONDARY_THREAD { assert( m_dbgThreadId != std::this_thread::get_id() ); }

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

    virtual~FlatDrivePaths() {}

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
    using lt_handle = Session::lt_handle;
    using uint128_t = boost::multiprecision::uint128_t;

    // UseTorrentInfo is used to avoid adding torrents into session with the same hash
    // and for deleting unused files and torrents from session
    struct UseTorrentInfo {
        lt_handle m_ltHandle = {};
        bool      m_isUsed = true;
    };

    LtSession           m_session;

    // It is as 1-st parameter in functions of ReplicatorEventHandler (for debugging)
    Replicator&         m_replicator;

    BackgroundExecutor  m_backgroundExecutor;

    size_t              m_maxSize;

    // List of replicators that support this drive
    ReplicatorList    m_replicatorList;
    
    // Replicator event handlers
    ReplicatorEventHandler&     m_eventHandler;
    DbgReplicatorEventHandler*  m_dbgEventHandler = nullptr;
    
    //
    bool m_modifyUserDataReceived       = false;
    bool m_sandboxCalculated            = false;
    bool m_modifyApproveTransactionSent = false;

    std::optional<Hash256>            m_receivedModifyApproveTx;
    
    //
    // Drive state
    //
    
    InfoHash                            m_rootHash;

    bool                                m_driveIsInitializing = true;
    std::optional<Hash256>              m_driveWillRemovedTx = {};
    bool                                m_modificationIsCanceling = false;
    std::optional<CatchingUpRequest>    m_catchingUpRequest;
    std::optional<ModifyRequest>        m_modifyRequest;

    //
    // Request queue
    //
    
    std::optional<Hash256>              m_removeDriveTx = {};
    std::optional<Hash256>              m_modificationMustBeCanceledTx;
    std::optional<CatchingUpRequest>    m_newCatchingUpRequest;
    std::deque<ModifyRequest>           m_defferedModifyRequests;

    //
    // Task data
    //
    bool                                 m_taskMustBeBroken = false;
    bool                                 m_taskIsBroken     = false;

    std::optional<lt_handle>             m_downloadingLtHandle; // used for removing torrent from session

    std::set<InfoHash>                  m_catchingUpFileSet;
    std::set<InfoHash>::iterator        m_catchingUpFileIt = m_catchingUpFileSet.end();

    // FsTree
    FsTree        m_fsTree;
    FsTree        m_sandboxFsTree;
    lt_handle     m_fsTreeLtHandle; // used for removing FsTree torrent from session

    // Root hashes
    InfoHash      m_sandboxRootHash;

    //
    // 'modify' opinion
    //
    std::optional<ApprovalTransactionInfo>  m_myOpinion;
    
    // It is needed for right calculation of my 'modify' opinion
    std::optional<Hash256>                  m_opinionTrafficIdentifier;
    uint64_t                                m_expectedCumulativeDownload;
    uint64_t                                m_accountedCumulativeDownload = 0;
    std::map<Key, uint64_t>                 m_cumulativeUploads;
    std::map<Key, uint64_t>                 m_lastAccountedUploads;

    // opinions from other replicators
    // key of the outer map is modification id
    // key of the inner map is a replicator key, one replicator one opinion
    std::map<Hash256, std::map<std::array<uint8_t,32>,ApprovalTransactionInfo>>    m_otherOpinions;
    
    std::optional<boost::asio::high_resolution_timer> m_modifyOpinionTimer;

    //
    // TorrentHandleMap is used to avoid adding torrents into session with the same hash
    // and for deleting unused files and torrents from session
    //
    std::map<InfoHash,UseTorrentInfo>       m_torrentHandleMap;

    // For debugging:
    const char*                             m_dbgOurPeerName = "";
    std::thread::id                         m_dbgThreadId;

public:
    DefaultFlatDrive(
                  std::shared_ptr<Session>  session,
                  const std::string&        replicatorRootFolder,
                  const std::string&        replicatorSandboxRootFolder,
                  const Key&                drivePubKey,
                  size_t                    maxSize,
                  size_t                    usedDriveSizeExcludingMetafiles,
                  ReplicatorEventHandler&   eventHandler,
                  Replicator&               replicator,
                  const ReplicatorList&     replicatorList,
                  DbgReplicatorEventHandler* dbgEventHandler )
        :
          FlatDrivePaths( replicatorRootFolder, replicatorSandboxRootFolder, drivePubKey ),
          m_session(session),
          m_replicator(replicator),
          m_backgroundExecutor(),
          m_maxSize(maxSize),
          m_replicatorList(replicatorList),
          m_eventHandler(eventHandler),
          m_dbgEventHandler(dbgEventHandler),
          m_expectedCumulativeDownload(usedDriveSizeExcludingMetafiles),
          m_dbgOurPeerName(replicator.dbgReplicatorName())
    {
        m_dbgThreadId = std::this_thread::get_id();
        
        // Initialize drive
        init();
    }

    virtual~DefaultFlatDrive() {
        terminate();
    }

    const Key& drivePublicKey() const override { return m_drivePubKey; }

    void terminate()
    {
        //TODO main thread
        LOG_ERR ("Not implemented");
        //TODO
    }

    uint64_t maxSize() const override {
        return m_maxSize;
    }

    InfoHash rootHash() const override {
        return m_rootHash;
    }
    
    ReplicatorList getReplicators() override {
        return m_replicatorList;
    }

    void updateReplicators(const ReplicatorList& replicators) override
    {
        DBG_MAIN_THREAD

        if (replicators.empty()) {
            LOG_ERR( "ReplicatorList is empty!");
            return;
        }

        for (const ReplicatorInfo& ri : replicators) {
            const auto& r = std::find(m_replicatorList.begin(), m_replicatorList.end(), ri);
            if(r != m_replicatorList.end()) {
                *r = ri;
            } else {
                m_replicatorList.push_back(ri);
            }
        }
    }
    
    uint64_t sandboxFsTreeSize() const override
    {
        if ( fs::exists(m_sandboxFsTreeFile) )
        {
            return fs::file_size( m_sandboxFsTreeFile );
        }
        return 0;
    }

    void getSandboxDriveSizes( uint64_t& metaFilesSize, uint64_t& driveSize ) const override
    {
        if ( fs::exists(m_sandboxRootPath) )
        {
            metaFilesSize = fs::file_size( m_sandboxFsTreeTorrent);
            driveSize = 0;
            m_sandboxFsTree.getSizes( m_driveFolder, m_torrentFolder, metaFilesSize, driveSize );
            driveSize += metaFilesSize;
        }
        else
        {
            metaFilesSize = 0;
            driveSize = 0;
        }
    }

    // Initialize drive
    void init()
    {
        DBG_MAIN_THREAD
        
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
        m_rootHash = createTorrentFile( m_fsTreeFile,
                                        m_drivePubKey,
                                        m_fsTreeFile.parent_path(),
                                        m_fsTreeTorrent );

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
        DBG_MAIN_THREAD
        
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
                
                // skip identical files that was already added
                if ( m_torrentHandleMap.find(getFile(*fsTreeChild).hash()) != m_torrentHandleMap.end() )
                {
                    auto torrentHandle = m_session->addTorrentFileToSession( torrentFile, m_driveFolder, lt::sf_is_replicator, {} );
                    m_torrentHandleMap.emplace( getFile(*fsTreeChild).hash(), UseTorrentInfo{ torrentHandle, true } );
                }
            }
        }
    }

    void downgradeCumulativeUploads()
    {
        DBG_MAIN_THREAD

        _ASSERT(m_modifyRequest)
        _ASSERT(m_modificationIsCanceling)

        // We have already taken into account information
        // about uploads of the modification to be canceled;
        if ( !m_opinionTrafficIdentifier )
        {
            uint64_t sum = 0;
            for (const auto&[uploaderKey, bytes]: m_lastAccountedUploads)
            {
                sum += bytes;
                m_cumulativeUploads[uploaderKey] -= bytes;
            }
            m_accountedCumulativeDownload -= sum;
            m_expectedCumulativeDownload = m_accountedCumulativeDownload;
        }
    }
    
    void runNextTask()
    {
        DBG_MAIN_THREAD

        //todo _ASSERT( !m_driveIsInitializing );
        
        m_catchingUpRequest.reset();
        m_modifyRequest.reset();
        m_modificationIsCanceling = false;
        
        if ( m_driveWillRemovedTx )
        {
            _ASSERT( !m_removeDriveTx );
            if ( !m_removeDriveTx )
            {
                runDriveClosingTask( std::move(m_removeDriveTx) );
            }
            return;
        }

        if ( m_modificationMustBeCanceledTx )
        {
            auto tx = *m_modificationMustBeCanceledTx;
            m_modificationMustBeCanceledTx.reset();
            
            runCancelModifyDriveTask( tx );
            return;
        }
        
        if ( m_newCatchingUpRequest )
        {
            startCatchingUp( *m_newCatchingUpRequest );
            return;
        }
        
        if ( !m_defferedModifyRequests.empty() )
        {
            auto request = std::move( m_defferedModifyRequests.front() );
            m_defferedModifyRequests.pop_front();
            startModifyDrive( std::move(request) );
        }
    }
    
    void breakTorrentDownload()
    {
        DBG_MAIN_THREAD
        
        if ( m_taskMustBeBroken )
            return;
        
        m_taskMustBeBroken = true;
        m_taskIsBroken     = false;

        if ( (m_modifyRequest || m_catchingUpRequest) && m_downloadingLtHandle )
        {
            //
            // We must break torrent downloading because it could be unavailable
            //
            m_session->removeDownloadContext( *m_downloadingLtHandle );
            
            lt_handle torrentHandle = *m_downloadingLtHandle;
            m_downloadingLtHandle.reset();

            m_session->removeTorrentsFromSession( {torrentHandle}, [=, this]
            {
                DBG_MAIN_THREAD
                _LOG( "breakTorrentDownload: torrent is removed");

                m_backgroundExecutor.run( [this]
                {
                    //TODO: move downloaded files from sandbox to drive (for catching-up only)
                    
                    fs::remove_all( m_sandboxRootPath );
                    
                    m_session->lt_session().get_context().post( [=,this]
                    {
                        runNextTask();
                    });
                });
            });
        }
        else
        {
            // (+++) Всегда ли так будет?
            // wait the end of the current task
        }
    }

    // It tries to start next modify
    void modifyIsCompleted()
    {
        DBG_MAIN_THREAD

        _ASSERT( m_modifyRequest.has_value() != m_catchingUpRequest.has_value() )

        m_fsTree = m_sandboxFsTree;
        m_rootHash = m_sandboxRootHash;

        Hash256 modificationHash;
        if ( m_modifyRequest )
        {
            _LOG( "modifyIsCompleted" );
            modificationHash = m_modifyRequest->m_transactionHash;
            m_modifyRequest.reset();
        }
        else {
            _LOG( "catchingUpIsCompleted" );
            modificationHash = m_catchingUpRequest->m_modifyTransactionHash;
            m_catchingUpRequest.reset();
        }

        if ( m_dbgEventHandler )
            m_dbgEventHandler->driveModificationIsCompleted( m_replicator, m_drivePubKey, modificationHash, m_rootHash );

        runNextTask();
    }

    //
    // CANCEL
    //

    void cancelModifyDrive( const Hash256& transactionHash ) override
    {
        DBG_MAIN_THREAD
        
        _ASSERT( m_modifyRequest || m_catchingUpRequest );
        
        if ( m_modifyRequest && transactionHash == m_modifyRequest->m_transactionHash )
        {
            if ( m_receivedModifyApproveTx )
            {
                LOG_ERR( "cancelModifyDrive(): m_receivedModifyApproveTx == true" )
                return;
            }

            m_modificationIsCanceling = true;
            m_modifyRequest->m_isCanceled = true;
            downgradeCumulativeUploads();
            
            breakTorrentDownload();
            return;
        }
        else
        {
            auto it = std::find_if( m_defferedModifyRequests.begin(), m_defferedModifyRequests.end(), [&transactionHash](const auto& item)
                                { return item.m_transactionHash == transactionHash; });
            
            if ( it == m_defferedModifyRequests.end() )
            {
                LOG_ERR( "cancelModifyDrive(): invalid transactionHash: " << transactionHash );
                return;
            }
            
            m_defferedModifyRequests.erase( it );
        }
    }

    void runCancelModifyDriveTask( const Hash256& transactionHash )
    {
        DBG_MAIN_THREAD

        _LOG("CONTINUE CANCEL");

        m_modifyRequest.reset();

        // clear sandbox folder
        fs::remove_all( m_sandboxRootPath );
        fs::create_directories( m_sandboxRootPath);

        m_modificationIsCanceling = false;
        
        m_eventHandler.driveModificationIsCanceled( m_replicator, drivePublicKey(), transactionHash );
        
        runNextTask();
    }
    
    //
    // CLOSE/REMOVE
    //
    
    void startDriveClosing( const Hash256& transactionHash ) override
    {
        DBG_MAIN_THREAD
        
        m_driveWillRemovedTx = transactionHash;

        {
            std::ofstream filestream( m_driveFolder / "is_closing" );
            filestream << "1";
            filestream.close();
        }
        
        if ( m_modifyRequest || m_catchingUpRequest )
        {
            breakTorrentDownload();
        }
        else
        {
            runNextTask();
        }
    }

    void runDriveClosingTask( std::optional<Hash256>&& transactionHash )
    {
        DBG_MAIN_THREAD
        
        _ASSERT( transactionHash );
        m_removeDriveTx = std::move(transactionHash);
        
        //
        // Remove torrents from session
        //
        
        std::set<lt_handle> tobeRemovedTorrents;

        for( auto& [key,value]: m_torrentHandleMap )
        {
            tobeRemovedTorrents.insert( value.m_ltHandle );
        }
        
        tobeRemovedTorrents.insert( m_fsTreeLtHandle );

        m_session->removeTorrentsFromSession( std::move(tobeRemovedTorrents), [this,transactionHash](){
            m_replicator.closeDriveChannels( *m_removeDriveTx, *this );
        });
    }

    void synchronizeDriveWithSandbox()
    {
        DBG_MAIN_THREAD
        
        if ( m_taskMustBeBroken )
        {
            m_session->lt_session().get_context().post( [=,this]
            {
                runNextTask();
            });
            return;
        }

        _ASSERT( m_sandboxCalculated );
        _ASSERT( !m_modifyRequest->m_isCanceled );
        if ( !m_modifyRequest )
            _ASSERT( !m_modifyRequest );

        updateDrive_1( [this]
        {
            updateDrive_2();
        });
    }

    // startModifyDrive - should be called after client 'modify request'
    //
    void startModifyDrive( ModifyRequest&& modifyRequest ) override
    {
        DBG_MAIN_THREAD
        
        m_modifyUserDataReceived  = false;

        m_replicatorList = modifyRequest.m_replicatorList;

        // ModificationIsCanceling check is redundant now
        if ( m_modifyRequest || m_catchingUpRequest || m_newCatchingUpRequest || m_modificationIsCanceling )
        {
            //LOG_ERR( "startModifyDrive():: prevoius modification is not completed" );
            m_defferedModifyRequests.emplace_back( std::move(modifyRequest) );
            return;
        }

        m_sandboxCalculated          = false;
        m_receivedModifyApproveTx.reset();
        m_modifyApproveTransactionSent     = false;
        m_modificationIsCanceling    = false;
        
        // remove my opinion
        m_myOpinion.reset();

        m_modifyRequest = std::move( modifyRequest );

        // clear client session folder
        fs::remove_all( m_sandboxRootPath );
        fs::create_directories( m_sandboxRootPath);

        using namespace std::placeholders;  // for _1, _2, _3

        m_expectedCumulativeDownload += m_modifyRequest->m_maxDataSize;

        _ASSERT( !m_opinionTrafficIdentifier );
        m_opinionTrafficIdentifier = m_modifyRequest->m_transactionHash;
        
        m_downloadingLtHandle = m_session->download( DownloadContext(
                                            DownloadContext::client_data,
                                            std::bind( &DefaultFlatDrive::modifyDownloadHandler, this, _1, _2, _3, _4, _5, _6 ),
                                            m_modifyRequest->m_clientDataInfoHash,
                                            *m_opinionTrafficIdentifier,
                                            0, //todo
                                            ""),
                                                   m_sandboxRootPath,
                                                   //{} );
                                            m_replicatorList );
    }

    // will be called by Session
    void modifyDownloadHandler( download_status::code code,
                                const InfoHash& infoHash,
                                const std::filesystem::path /*filePath*/,
                                size_t /*downloaded*/,
                                size_t /*fileSize*/,
                                const std::string& errorText )
    {
        DBG_MAIN_THREAD
    
        if ( !m_modifyRequest )
        {
            m_eventHandler.modifyTransactionEndedWithError( m_replicator, m_drivePubKey, {}, "DefaultDrive::downloadHandler: internal error", 0 );
            modifyIsCompleted();
            return;
        }

        if ( m_modifyRequest->m_clientDataInfoHash != infoHash )
        {
            m_eventHandler.modifyTransactionEndedWithError( m_replicator, m_drivePubKey, *m_modifyRequest, "DefaultDrive::downloadHandler: internal error", 0 );
            modifyIsCompleted();
            return;
        }

        if ( code == download_status::failed )
        {
            m_eventHandler.modifyTransactionEndedWithError( m_replicator, m_drivePubKey, *m_modifyRequest, errorText, 0 );
            modifyIsCompleted();
            return;
        }

        if ( code == download_status::download_complete )
        {
            m_modifyUserDataReceived = true;
            m_backgroundExecutor.run( [this]
            {
                modifyDriveInSandbox();
            });
        }
    }

    // client data is received,
    // so we start drive modification
    //
    void modifyDriveInSandbox()
    {
        DBG_SECONDARY_THREAD

        _ASSERT( m_modifyRequest.has_value() != m_catchingUpRequest.has_value() )

        if ( m_modifyRequest )
        {

            // Check that client data exist
            if ( !fs::exists(m_clientDataFolder) || !fs::is_directory(m_clientDataFolder) )
            {
                LOG( "m_clientDataFolder=" << m_clientDataFolder );
                m_eventHandler.modifyTransactionEndedWithError( m_replicator, m_drivePubKey, *m_modifyRequest, "modify drive: 'client-data' is absent", -1 );
                m_session->lt_session().get_context().post( [=,this]
                {
                    modifyIsCompleted();
                });
                return;
            }

            // Check 'actionList.bin' is received
            if ( !fs::exists( m_clientActionListFile ) )
            {
                LOG( "m_clientActionListFile=" << m_clientActionListFile );
                m_eventHandler.modifyTransactionEndedWithError( m_replicator, m_drivePubKey, *m_modifyRequest, "modify drive: 'ActionList.bin' is absent", -1 );
                m_session->lt_session().get_context().post( [=,this]()
                {
                    modifyIsCompleted();
                });
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
                        InfoHash fileHash = calculateInfoHashAndCreateTorrentFile( clientFile, m_drivePubKey, m_torrentFolder, "" );
                        size_t fileSize = std::filesystem::file_size( clientFile );

                        // add ref into 'torrentMap' (skip if identical file was already loaded)
                        m_torrentHandleMap.try_emplace( fileHash, UseTorrentInfo{} );

                        // rename file and move it into drive folder
                        std::string newFileName = m_driveFolder / hashToFileName( fileHash );
                        fs::rename( clientFile, newFileName );

                        //
                        // add file in resultFsTree
                        //
                        Folder::Child* destEntry = m_sandboxFsTree.getEntryPtr( action.m_param2 );
                        fs::path destFolder;
                        fs::path srcFile;
                        if ( destEntry != nullptr && isFolder(*destEntry) )
                        {
                            srcFile = fs::path( action.m_param1 ).filename();
                            destFolder = action.m_param2;
                        }
                        else
                        {
                            srcFile = fs::path( action.m_param2 ).filename();
                            destFolder = fs::path(action.m_param2).parent_path();
                        }
                        m_sandboxFsTree.addFile( destFolder,
                                                 srcFile,
                                                 fileHash,
                                                 fileSize );
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
                            _LOG( "invalid 'remove' action: src not exists (in FsTree): " << action.m_param1  );
                            //m_sandboxFsTree.dbgPrint();
                            action.m_isInvalid = true;
                            break;
                        }

                        // remove entry from FsTree
                        m_sandboxFsTree.removeFlat( action.m_param1, [this] ( const InfoHash& fileHash )
                        {
                            // maybe it is file from client data, so we add it to map with empty torrent handle
                            m_torrentHandleMap.try_emplace( fileHash, UseTorrentInfo{} );
                        } );

                        break;
                    }

                } // end of switch()
            } // end of for( const Action& action : actionList )

            // create FsTree in sandbox
            m_sandboxFsTree.doSerialize( m_sandboxFsTreeFile );
        }
        else {
            for( const auto& fileHash : m_catchingUpFileSet )
            {
                auto fileName = toString( fileHash );

                // move file to drive folder
                fs::rename(  m_sandboxRootPath / fileName, m_driveFolder / fileName );

                // create torrent
                calculateInfoHashAndCreateTorrentFile( m_driveFolder / fileName, m_drivePubKey, m_torrentFolder, "" );
            }
        }
        m_sandboxRootHash = createTorrentFile( m_sandboxFsTreeFile,
                                               m_drivePubKey,
                                               m_sandboxRootPath,
                                               m_sandboxFsTreeTorrent );

        m_session->lt_session().get_context().post( [=,this]()
        {
            myRootHashIsCalculated();
        });
    }

    void normalizeUploads(std::map<Key, uint64_t>& modificationUploads, uint64_t targetSum)
    {
        uint128_t longTargetSum = targetSum;
        uint128_t sumBefore = std::accumulate(modificationUploads.begin(),
                                              modificationUploads.end(),
                                              0,
                                              [] (const uint64_t& value, const std::pair<Key, int>& p)
                                              { return value + p.second; }
                                              );
        
        uint64_t sumAfter = 0;

        if ( sumBefore > 0 ) // (+++) ?
        {
            for ( auto& [key, uploadBytes]: modificationUploads ) {
                if ( key != m_modifyRequest->m_clientPublicKey )
                {
                    _LOG( "sumBefore: " << sumBefore );
                    auto longUploadBytes = (uploadBytes * longTargetSum) / sumBefore;
                    uploadBytes = longUploadBytes.convert_to<uint64_t>();
                    sumAfter += uploadBytes;
                }
            }
            modificationUploads[m_modifyRequest->m_clientPublicKey] = targetSum - sumAfter;
        }
    }

    void updateCumulativeUploads()
    {
        const auto &modifyTrafficMap = m_replicator.getMyDownloadOpinion(*m_opinionTrafficIdentifier)
                .m_modifyTrafficMap;

        m_lastAccountedUploads.clear();
        for (const auto &replicatorIt : m_replicatorList)
        {
            // get data size received from 'replicatorIt.m_publicKey'
            if (auto it = modifyTrafficMap.find(replicatorIt.m_publicKey.array());
                    it != modifyTrafficMap.end())
            {
                m_lastAccountedUploads[it->first] = it->second.m_receivedSize;
            } else
            {
                m_lastAccountedUploads[it->first] = 0;
            }
        }

        if (auto it = modifyTrafficMap.find(m_modifyRequest->m_clientPublicKey.array());
                it != modifyTrafficMap.end())
        {
            m_lastAccountedUploads[it->first] = it->second.m_receivedSize;
        } else
        {
            m_lastAccountedUploads[it->first] = 0;
        }

        uint64_t targetSize = m_expectedCumulativeDownload - m_accountedCumulativeDownload;
        normalizeUploads(m_lastAccountedUploads, targetSize);
        m_accountedCumulativeDownload = m_expectedCumulativeDownload;
        m_opinionTrafficIdentifier.reset();

        for (const auto&[uploaderKey, bytes]: m_lastAccountedUploads)
        {
            if (m_cumulativeUploads.find(uploaderKey) == m_cumulativeUploads.end())
            {
                m_cumulativeUploads[uploaderKey] = 0;
            }
            m_cumulativeUploads[uploaderKey] += bytes;
        }
    }
    
    void createMyOpinion()
    {
        DBG_MAIN_THREAD

        _ASSERT( m_opinionTrafficIdentifier )
        _ASSERT( m_modifyRequest.has_value() != m_catchingUpRequest.has_value() )
        _ASSERT( !m_modificationIsCanceling )

        updateCumulativeUploads();

        //
        // Calculate upload opinion
        //
        SingleOpinion opinion( m_replicator.replicatorKey().array() );
        for( const auto& replicatorIt : m_replicatorList )
        {
            auto it = m_cumulativeUploads.find( replicatorIt.m_publicKey.array() );
            opinion.m_uploadLayout.push_back( {replicatorIt.m_publicKey.array(), it->second} );
        }

        {
            auto it = m_cumulativeUploads.find( m_modifyRequest->m_clientPublicKey.array() );
            opinion.m_clientUploadBytes = it->second;
        }

        // Calculate size of torrent files and total drive size
        uint64_t metaFilesSize;
        uint64_t driveSize;
        getSandboxDriveSizes( metaFilesSize, driveSize );
        uint64_t fsTreeSize = sandboxFsTreeSize();

        _LOG( "Drive Size " << driveSize);

        Hash256 modificationToBeApproved;
        if ( m_modifyRequest )
        {
            modificationToBeApproved = m_modifyRequest->m_transactionHash;
        }
        else
        {
            modificationToBeApproved = m_catchingUpRequest->m_modifyTransactionHash;
        }

        opinion.Sign( m_replicator.keyPair(),
                      drivePublicKey(),
                      modificationToBeApproved,
                      m_sandboxRootHash,
                      fsTreeSize,
                      metaFilesSize,
                      driveSize);

        m_myOpinion = std::optional<ApprovalTransactionInfo> {{ m_drivePubKey.array(),
                                                                modificationToBeApproved.array(),
                                                                m_sandboxRootHash.array(),
                                                                fsTreeSize,
                                                                metaFilesSize,
                                                                driveSize,
                                                                { std::move(opinion) }}};
    }

    void myRootHashIsCalculated()
    {
        DBG_MAIN_THREAD

        _ASSERT( m_modifyRequest.has_value() != m_catchingUpRequest.has_value() )
        
        if ( m_modificationIsCanceling )
            return;
        
        // Notify
        if ( m_dbgEventHandler )
            m_dbgEventHandler->rootHashIsCalculated( m_replicator, m_drivePubKey, m_modifyRequest->m_transactionHash, m_sandboxRootHash );
        
        // Calculate my opinion
        createMyOpinion();

        m_sandboxCalculated = true;

        if ( m_receivedModifyApproveTx )
        {
            auto transactionHash = m_modifyRequest->m_transactionHash;
            sendSingleApprovalTransaction();
            if ( m_catchingUpRequest )
            {
                updateDrive_1( [this]
                {
                    completeCatchingUp();
                });
            }
            else
            {
                synchronizeDriveWithSandbox();
            }
        }
        else
        {
            _ASSERT( m_modifyRequest )

            // Send my opinion to other replicators
            shareMyOpinion();

            // validate already received opinions
            auto& transactionOpinions = m_otherOpinions[m_modifyRequest->m_transactionHash];
            std::erase_if( transactionOpinions, [this] (const auto& item) {
               return !validateOpinion(item.second);
            });

            // Maybe send approval transaction
            checkOpinionNumberAndStartTimer();
        }
    }
    
    void shareMyOpinion()
    {
        DBG_MAIN_THREAD
        
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
        DBG_MAIN_THREAD
        
        // m_replicatorList is the list of other replicators (it does not contain our replicator)
        auto replicatorNumber = m_modifyRequest->m_replicatorList.size();//todo++++ +1;

        // check opinion number
        if ( m_myOpinion && m_otherOpinions[m_modifyRequest->m_transactionHash].size() >= ((replicatorNumber)*2)/3
            && !m_modifyApproveTransactionSent && !m_receivedModifyApproveTx )
        {
            // start timer if it is not started
            if ( !m_modifyOpinionTimer )
                m_modifyOpinionTimer = m_session->startTimer( m_replicator.getModifyApprovalTransactionTimerDelay(),
                                    [this]() { opinionTimerExpired(); } );
        }
    }
    
    // updates drive (1st step after approve)
    // - remove torrents from session
    //
    void updateDrive_1( const std::function<void()>& nextStep )
    {
        DBG_MAIN_THREAD
        
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
        m_session->removeTorrentsFromSession( toBeRemovedTorrents, [this, nextStep]
        {
            m_backgroundExecutor.run( [nextStep]
            {
                nextStep();
            });
        });
    }

    // updates drive (2st phase after fsTree torrent removed)
    // - remove unused files and torrent files
    // - add new torrents to session
    //
    void updateDrive_2()
    {
        DBG_SECONDARY_THREAD
        
        try
        {
            _LOG( "IN UPDATE 2")

            // update FsTree file & torrent
            fs::rename( m_sandboxFsTreeFile, m_fsTreeFile );
            fs::rename( m_sandboxFsTreeTorrent, m_fsTreeTorrent );

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
            _LOG( "Add FsTree torrent to session: " << toString(m_rootHash) );
            m_fsTreeLtHandle = m_session->addTorrentFileToSession( m_fsTreeTorrent,
                                                                   m_fsTreeTorrent.parent_path(),
                                                                   lt::sf_is_replicator );

            // clear sandbox
            fs::remove_all( m_sandboxRootPath );
            m_session->lt_session().get_context().post( [=,this]
            {
                modifyIsCompleted();
            });
        }
        catch ( const std::exception& ex )
        {
            _LOG( "???: updateDrive_2 broken: " << ex.what() );
            m_session->lt_session().get_context().post( [=,this]
            {
                runNextTask();
            });
        }
    }

    // Recursively marks 'm_toBeRemoved' as false
    //
    void markUsedFiles( const Folder& folder )
    {
        DBG_MAIN_THREAD
        
        for( const auto& child : folder.m_childs )
        {
            if ( isFolder(child) )
            {
                markUsedFiles( getFolder(child) );
            }
            else
            {
                auto& hash = getFile(child).m_hash;
                
                if ( const auto& it = m_torrentHandleMap.find(hash); it != m_torrentHandleMap.end() )
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

    bool validateOpinion(const ApprovalTransactionInfo& anOpinion ) {
        bool equal = m_myOpinion->m_rootHash == anOpinion.m_rootHash &&
                     m_myOpinion->m_fsTreeFileSize == anOpinion.m_fsTreeFileSize &&
                     m_myOpinion->m_metaFilesSize == anOpinion.m_metaFilesSize &&
                     m_myOpinion->m_driveSize == anOpinion.m_driveSize;
        return equal;
    }

    void onOpinionReceived( const ApprovalTransactionInfo& anOpinion ) override
    {
        DBG_MAIN_THREAD
        
        // Preliminary opinion verification takes place at extension

        // In this case Replicator is able to verify all data in the opinion
        if ( m_modifyRequest &&
            anOpinion.m_modifyTransactionHash == m_modifyRequest->m_transactionHash.array() &&
            m_myOpinion)
        {
            if ( !validateOpinion(anOpinion) )
            {
                return;
            }
        }

        // May be send approval transaction
        m_otherOpinions[anOpinion.m_modifyTransactionHash][anOpinion.m_opinions[0].m_replicatorKey] = anOpinion;
        checkOpinionNumberAndStartTimer();
    }
    
    void opinionTimerExpired()
    {
        DBG_MAIN_THREAD
        
        if ( m_modifyApproveTransactionSent || m_receivedModifyApproveTx )
            return;
        
        if ( m_modificationIsCanceling )
            return;
        
        ApprovalTransactionInfo info = {    m_drivePubKey.array(),
                                            m_myOpinion->m_modifyTransactionHash,
                                            m_myOpinion->m_rootHash,
                                            m_myOpinion->m_fsTreeFileSize,
                                            m_myOpinion->m_metaFilesSize,
                                            m_myOpinion->m_driveSize,
                                            {}};
        
        info.m_opinions.reserve( m_otherOpinions[m_modifyRequest->m_transactionHash].size()+1 );
        info.m_opinions.emplace_back(  m_myOpinion->m_opinions[0] );
        for( const auto& otherOpinion : m_otherOpinions[m_modifyRequest->m_transactionHash] ) {
            info.m_opinions.emplace_back( otherOpinion.second.m_opinions[0] );
        }
        
        // notify event handler
        _LOG( "*************" )
        m_eventHandler.modifyApprovalTransactionIsReady( m_replicator, std::move(info) );
        
        m_replicator.removeModifyDriveInfo( m_myOpinion->m_modifyTransactionHash );
        
        m_modifyApproveTransactionSent = true;
    }

    void onApprovalTransactionHasBeenPublished( const ApprovalTransactionInfo& transaction ) override
    {
        DBG_MAIN_THREAD

        if ( m_receivedModifyApproveTx &&  *m_receivedModifyApproveTx == transaction.m_modifyTransactionHash )
        {
            LOG_ERR("Duplicated approval tx ");
            return;
        }

        m_receivedModifyApproveTx = transaction.m_modifyTransactionHash;

        // stop timer
        m_modifyOpinionTimer.reset();
        m_otherOpinions.erase(transaction.m_modifyTransactionHash);
        
        // check actual root hash
        if ( m_modificationIsCanceling )
        {
            // wait the end of 'Cancel Modification'
            // and then start 'CatchingUp'
            m_newCatchingUpRequest = { transaction.m_rootHash, transaction.m_modifyTransactionHash };
            return;
        }

        if ( m_catchingUpRequest && m_catchingUpRequest->m_rootHash == transaction.m_rootHash )
        {
            // TODO We should update knowledge about the catching modification id
            // This situation could be valid if some next modification has not changed Drive Root Hash
            // For example, because of next modification was invalid
            // So, we continue previous catching-up
            return;
        }

        // We should not catch up only in the case
        // when we have already downloaded all necessary data (??? or the modification approval)
        bool couldContinueModify =
                m_modifyRequest &&
                m_modifyRequest->m_transactionHash == transaction.m_modifyTransactionHash &&
                m_modifyUserDataReceived;
        
        // We must start new catch up
        // if current is outdated
        bool shouldCatchUp = (m_catchingUpRequest    && m_catchingUpRequest->m_rootHash    != transaction.m_rootHash) ||
                             (m_newCatchingUpRequest && m_newCatchingUpRequest->m_rootHash != transaction.m_rootHash);

        _LOG( "shouldCatchUp, couldContinueModify: " << shouldCatchUp << " " << couldContinueModify )
        if ( shouldCatchUp || !couldContinueModify )
        {
            _LOG( "Will catch-up" )
            m_newCatchingUpRequest = { transaction.m_rootHash, transaction.m_modifyTransactionHash };

            // We should break torrent downloading
            // Because they (torrents/files) may no longer be available
            breakTorrentDownload();
            return;
        }

        if ( !m_sandboxCalculated )
        {
            // wait the end of sandbox calculation
            return;
        }
        else
        {
            const auto& v = transaction.m_opinions;
            auto it = std::find_if( v.begin(), v.end(), [this] (const auto& opinion) {
                return opinion.m_replicatorKey == m_replicator.replicatorKey().array();
            });

            // Is my opinion present
            if ( it == v.end() )
            {
                // Send Single Aproval Transaction At First
                sendSingleApprovalTransaction();
            }

            synchronizeDriveWithSandbox();
        }
    }

    void onApprovalTransactionHasFailedInvalidSignatures(const Hash256 &transactionHash) override
    {
        DBG_MAIN_THREAD

        if ( m_modifyRequest &&
             m_modifyRequest->m_transactionHash == transactionHash &&
             !m_modifyRequest->m_isCanceled &&
             !m_receivedModifyApproveTx)
        {
            if ( auto it = m_otherOpinions.find(transactionHash); it != m_otherOpinions.end() )
            {
                m_modifyApproveTransactionSent = false;
                auto opinions = it->second;
                m_otherOpinions.erase(it);
                for (const auto& [key, opinion]: opinions) {
                    m_replicator.processOpinion(opinion);
                }
            }
        }
    }

    void sendSingleApprovalTransaction()
    {
        DBG_MAIN_THREAD

        auto copy = *m_myOpinion;
        m_eventHandler.singleModifyApprovalTransactionIsReady( m_replicator, std::move(copy) );
    }

    void onSingleApprovalTransactionHasBeenPublished( const ApprovalTransactionInfo& transaction ) override
    {
        _LOG( "onSingleApprovalTransactionHasBeenPublished()" );
    }

    void startCatchingUp( std::optional<CatchingUpRequest>&& actualCatchingRequest ) override
    {
        DBG_MAIN_THREAD
        
        _ASSERT( !m_modificationIsCanceling );
        _ASSERT( !m_modifyRequest );
        _ASSERT( !m_removeDriveTx );

        // actualRootHash could be empty when internal error ONLY
        if ( actualCatchingRequest )
        {
            m_newCatchingUpRequest.reset();

            // clear modification queue - we will not execute these modifications
            auto it = std::find_if(m_defferedModifyRequests.begin(),
                                   m_defferedModifyRequests.end(),
                                   [&](const auto &item)
                                   {
                                       return item.m_transactionHash ==
                                              actualCatchingRequest->m_modifyTransactionHash;
                                   });
            if ( it != m_defferedModifyRequests.end() )
            {
                it++;
                while ( !m_defferedModifyRequests.empty() and it != m_defferedModifyRequests.begin() )
                {
                    m_expectedCumulativeDownload += m_defferedModifyRequests.front().m_maxDataSize;
                    m_defferedModifyRequests.pop_front();
                }
            }
            
            m_catchingUpRequest = std::move(actualCatchingRequest );

            fs::remove_all( m_sandboxRootPath );
            fs::create_directories( m_sandboxRootPath);
        }
        
        //
        // Start download fsTree
        //
        using namespace std::placeholders;  // for _1, _2, _3

        _LOG( "Late: download FsTree:" << m_catchingUpRequest->m_rootHash )
        
        if ( !m_opinionTrafficIdentifier )
        {
            m_opinionTrafficIdentifier = m_catchingUpRequest->m_modifyTransactionHash;
        }
        m_downloadingLtHandle = m_session->download( DownloadContext(
                                            DownloadContext::missing_files,
                                            std::bind( &DefaultFlatDrive::catchingUpFsTreeDownloadHandler, this, _1, _2, _3, _4, _5, _6 ),
                                            m_catchingUpRequest->m_rootHash,
                                            *m_opinionTrafficIdentifier,
                                            0,
                                            "" ),
                                            //toString( *m_catchingUpRootHash ) ),
                                            m_sandboxRootPath,
                                                   //{} );
                                            m_replicatorList );
    }
    
    // it will be called from Session
    void catchingUpFsTreeDownloadHandler( download_status::code code,
                                          const InfoHash& infoHash,
                                          const std::filesystem::path /*filePath*/,
                                          size_t /*downloaded*/,
                                          size_t /*fileSize*/,
                                          const std::string& errorText )
    {
        DBG_MAIN_THREAD

        _ASSERT( !m_newCatchingUpRequest )

        if ( code == download_status::failed )
        {
            //todo is it possible?
            _ASSERT(0);
            return;
        }

        if ( code == download_status::download_complete )
        {
            startDownloadMissingFiles();
        }
    }

    void startDownloadMissingFiles()
    {
        DBG_MAIN_THREAD

        _LOG( "startDownloadMissingFiles: removeTorrentsFromSession: " << m_downloadingLtHandle->id()  << " " << m_downloadingLtHandle->info_hashes().v2  );

        //
        // Deserialize FsTree
        //
        try
        {
            m_sandboxFsTree.deserialize( m_sandboxFsTreeFile );
            m_downloadingLtHandle.reset();
        }
        catch(...)
        {
            LOG_ERR( "cannot deserialize 'CatchingUpFsTree'" );
            fs::remove( m_sandboxFsTreeFile );
        }
        
        //
        // Prepare missing list and start download
        //
        m_catchingUpFileSet.clear();
        createCatchingUpFileList( m_sandboxFsTree );
        
        m_catchingUpFileIt = m_catchingUpFileSet.begin();
        downloadMissingFiles();
    }
    
    void createCatchingUpFileList( const Folder& folder )
    {
        DBG_MAIN_THREAD

        for( const auto& child : folder.m_childs )
        {
            if ( isFolder(child) )
            {
                createCatchingUpFileList( getFolder(child) );
            }
            else
            {
                const auto& hash = getFile(child).m_hash;
                
                if ( !fs::exists( m_driveFolder / toString(hash) ) )
                {
                    m_catchingUpFileSet.emplace( hash );
                }
            }
        }
    }

    // Download file by file
    void downloadMissingFiles()
    {
        DBG_MAIN_THREAD
        
        m_downloadingLtHandle.reset();

        if ( m_catchingUpFileIt == m_catchingUpFileSet.end() )
        {
            // it is the end of list
            m_backgroundExecutor.run([this]
            {
                modifyDriveInSandbox();
            });
        }
        else
        {
            if ( m_newCatchingUpRequest && m_newCatchingUpRequest->m_rootHash == m_catchingUpRequest->m_rootHash )
            {
                // TODO Check this situation
                LOG_ERR( "Not Implemented" );
                return;
            }

            auto missingFileHash = *m_catchingUpFileIt;
            m_catchingUpFileIt++;

            m_downloadingLtHandle = m_session->download( DownloadContext(
                                                                         
                                                 DownloadContext::missing_files,

                                                 [this] ( download_status::code code,
                                                           const InfoHash& infoHash,
                                                           const std::filesystem::path saveAs,
                                                           size_t /*downloaded*/,
                                                           size_t /*fileSize*/,
                                                           const std::string& errorText )
                                                 {
                                                     if ( code == download_status::download_complete )
                                                     {
                                                         _LOG( "catchedUp: " << toString(infoHash) );
                                                         downloadMissingFiles();
                                                     }
                                                     else if ( code == download_status::failed )
                                                     {
                                                         LOG_ERR("???");
                                                     }
                                                 },

                                                 missingFileHash,
                                                 *m_opinionTrafficIdentifier,
                                                 0,
                                                 ""),
                                                 m_sandboxRootPath,
                                                 m_replicatorList );
        }
    }
    
//    void completeCatchingUp_1()
//    {
//        DBG_MAIN_THREAD
//
//        //
//        // Create list of unused torrents
//        //
//
//        for( auto& it : m_torrentHandleMap )
//        {
//            it.second.m_isUsed = false;
//        }
//
//        markUsedFiles( m_sandboxFsTree );
//
//        std::set<lt_handle> tobeRemovedTorrents;
//
//        for( const auto& it : m_torrentHandleMap )
//        {
//            if ( !it.second.m_isUsed )
//            {
//                tobeRemovedTorrents.insert( it.second.m_ltHandle );
//            }
//        }
//        tobeRemovedTorrents.insert( m_fsTreeLtHandle );
//
//        //
//        // Remove unused torrents and files
//        //
//        m_session->removeTorrentsFromSession( tobeRemovedTorrents, [this]
//        {
//            DBG_MAIN_THREAD
//
//            // remove unused files and torrent files from the drive
//            for( const auto& [key,value] : m_torrentHandleMap )
//            {
//                if ( !value.m_isUsed )
//                {
//                    std::string filename = hashToFileName( key );
//                    fs::remove( fs::path(m_driveFolder) / filename );
//                    fs::remove( fs::path(m_torrentFolder) / filename );
//                }
//            }
//            fs::remove( fs::path(m_fsTreeFile) );
//
//            m_stopSecondaryThread = false;
//
//            if ( m_secondaryThread.get_id() != std::thread::id{} )
//                m_secondaryThread.detach();
//
//            m_secondaryThread = std::thread( [this]
//            {
//                completeCatchingUp_2();
//            });
//        });
//    }
    
    void completeCatchingUp()
    {
        DBG_SECONDARY_THREAD
        
        try
        {
            //
            // Check RootHash Before All
            //
            _LOG("m_sandboxRootHash: " << m_sandboxRootHash );
            _LOG("m_catchingUpRootHash: " << m_catchingUpRequest->m_rootHash );
            _ASSERT( m_sandboxRootHash == m_catchingUpRequest->m_rootHash );

            // check if RootHash has been published
            _ASSERT( !m_newCatchingUpRequest )

            fs::rename( m_sandboxFsTreeFile, m_fsTreeFile );
            fs::rename( m_sandboxFsTreeTorrent, m_fsTreeTorrent );

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
                }
            }

            //
            // Add missing files
            //
            for( const auto& fileHash : m_catchingUpFileSet )
            {
                auto fileName = toString( fileHash );

                // Add torrent into session
                auto tHandle = m_session->addTorrentFileToSession( m_torrentFolder / fileName,
                                                                   m_driveFolder,
                                                                   lt::sf_is_replicator );

                _ASSERT( tHandle.is_valid() );
                m_torrentHandleMap.try_emplace( fileHash, UseTorrentInfo{tHandle,true} );
            }

            // Add FsTree torrent to session
            m_fsTreeLtHandle = m_session->addTorrentFileToSession( m_fsTreeTorrent,
                                                                   m_fsTreeTorrent.parent_path(),
                                                                   lt::sf_is_replicator );

            // remove unused data from 'torrentMap'
            std::erase_if( m_torrentHandleMap, [] (const auto& it) { return !it.second.m_isUsed; } );

            LOG( "drive is synchronized" );

            // clear sandbox
            fs::remove_all( m_sandboxRootPath );
            
            m_session->lt_session().get_context().post( [=,this]
            {
                modifyIsCompleted();
            });
        }
        catch ( const std::exception& ex )
        {
            _LOG( "???: completeCatchingUp broken: " << ex.what() );
            m_session->lt_session().get_context().post( [=,this]
            {
                runNextTask();
            });
        }
    }
    
// It will be used after restart to clear disc !!!
//    void fillUsedFileList( const Folder& folder, InfoHashPtrSet& usedFileList )
//    {
//        for( const auto& child : folder.m_childs )
//        {
//            if ( isFolder(child) )
//            {
//                fillUsedFileList( getFolder(child), usedFileList );
//            }
//            else
//            {
//                auto& hash = getFile(child).m_hash;
//
//                if ( !fs::exists( m_driveFolder / toString(hash) ) )
//                {
//                    usedFileList.emplace( &hash );
//                }
//                if ( !fs::exists( m_torrentFolder / toString(hash) ) )
//                {
//                    usedFileList.emplace( &hash );
//                }
//            }
//        }
//    }
//
// It will be used after restart to clean disc !!!
//    void removeUnusedFiles( const FsTree& fsTree )
//    {
//        InfoHashPtrSet usedFileList;
//        fillUsedFileList( fsTree, usedFileList );
//
//        std::set<fs::path> tobeRemovedFileList;
//        for( const auto& entry: std::filesystem::directory_iterator{m_driveFolder} )
//        {
//            if ( entry.is_directory() )
//            {
//                _ASSERT(0);
//            }
//            else
//            {
//                // stem() - filename without extension
//                const InfoHash hash = stringToHash( entry.path().stem().string() );
//                if ( usedFileList.find( &hash ) == usedFileList.end() )
//                    tobeRemovedFileList.insert( entry.path().stem() );
//            }
//        }
//
//        for( const auto& file : tobeRemovedFileList )
//        {
//            fs::remove( file );
//        }
//    }

    bool isOutOfSync() const override
    {
        return m_catchingUpRequest.has_value();
    }

    const std::optional<Hash256>& closingTxHash() const override
    {
        return m_removeDriveTx;
    }
    
    void removeAllDriveData() override
    {
        DBG_MAIN_THREAD
        
        {
            // When node is restaring and file "is_closing" exists, but file "approval_tx_has_been_bulished" is not exists,
            // then drive should approve all download channels
            //todo : where replicator will find channels ids???
            std::ofstream filestream( m_driveFolder / "approval_tx_has_been_bulished" );
            filestream << "1";
            filestream.close();
        }

        fs::remove_all( m_driveRootPath );
        fs::remove_all( m_sandboxRootPath );
        
        m_eventHandler.driveIsClosed( m_replicator, m_drivePubKey, *m_removeDriveTx );
    }

    const ReplicatorList&  replicatorList() const override
    {
        return m_replicatorList;
    }

    void printDriveStatus() override
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
        size_t                   usedDriveSizeExcludingMetafiles,
        ReplicatorEventHandler&  eventHandler,
        Replicator&              replicator,
        const ReplicatorList&    replicators,
        DbgReplicatorEventHandler* dbgEventHandler )

{
    return std::make_shared<DefaultFlatDrive>( session,
                                           replicatorRootFolder,
                                           replicatorSandboxRootFolder,
                                           drivePubKey,
                                           maxSize,
                                           usedDriveSizeExcludingMetafiles,
                                           eventHandler,
                                           replicator,
                                           replicators,
                                           dbgEventHandler );
}

}

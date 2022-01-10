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
#include <cereal/types/map.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/archives/portable_binary.hpp>

#include <filesystem>
#include <set>
#include <functional>
#include <future>
#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include <shared_mutex>

#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>

#include <boost/multiprecision/cpp_int.hpp>
#include <boost/crc.hpp>
#include <numeric>

namespace fs = std::filesystem;

namespace sirius::drive {

#define DBG_MAIN_THREAD { assert( m_dbgThreadId == std::this_thread::get_id() ); }
#define DBG_BG_THREAD { assert( m_dbgThreadId != std::this_thread::get_id() ); }
#define DBG_VERIFY_THREAD { assert( m_verifyThread.get_id() == std::this_thread::get_id() ); }

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

    // Client data paths (received action list and files)
    const fs::path  m_clientDataFolder      = m_sandboxRootPath / "client-data";
    const fs::path  m_clientDriveFolder     = m_clientDataFolder / "drive";
    const fs::path  m_clientActionListFile  = m_clientDataFolder / "actionList.bin";

    // Restart data
    const fs::path  m_restartRootPath       = m_driveRootPath  / "restart-data";
    const fs::path  m_driveIsClosingPath    = m_driveRootPath  / "restart-data" / "drive-is-closing";
};

std::string FlatDrive::driveIsClosingPath( const std::string& driveRootPath )
{
    return fs::path(driveRootPath) / "restart-data" / "drive-is-closing";
}


struct UnknownVerifyCode
{
    const std::array<uint8_t,32>& m_tx;
    const std::array<uint8_t,32>& m_replicatorKey;
    uint64_t                      m_verificationCode;
};

//
// DefaultDrive - it manages all user files at replicator side
//
class DefaultFlatDrive: public FlatDrive, protected FlatDrivePaths {

    using lt_handle = Session::lt_handle;
    using uint128_t = boost::multiprecision::uint128_t;

    // UseTorrentInfo is used to avoid adding torrents into session with the same hash
    // and for deleting unused files and torrents from session
    struct UseTorrentInfo {
        lt_handle m_ltHandle = {};
        bool      m_isUsed = true;
    };

    std::weak_ptr<Session>  m_session;

    // It is as 1-st parameter in functions of ReplicatorEventHandler (for debugging)
    Replicator&             m_replicator;

    BackgroundExecutor      m_backgroundExecutor;
    std::thread             m_verifyThread;

    size_t                  m_maxSize;

    // List of replicators that support this drive
    ReplicatorList         m_replicatorList;

    // Client of the drive
    Key                     m_client;

    // Replicator event handlers
    ReplicatorEventHandler&     m_eventHandler;
    DbgReplicatorEventHandler*  m_dbgEventHandler = nullptr;
    

    // Modify status
    bool                        m_modifyUserDataReceived       = false;
    bool                        m_sandboxCalculated            = false;
    bool                        m_isSynchronizing              = false;
    bool                        m_modifyApproveTransactionSent = false;
    std::optional<Hash256>      m_receivedModifyApproveTx;

    // Verify status
    bool                            m_myVerifyCodesCalculated = false;
    bool                            m_verifyApproveTxSent   = false;
    std::optional<Hash256>          m_receivedVerifyApproveTx;
    std::vector<mobj<VerificationCodeInfo>> m_unknownVerificationCodeQueue;
    std::map<std::array<uint8_t,32>,mobj<VerificationCodeInfo>> m_receivedVerificationCodes;
    
    mobj<VerifyApprovalTxInfo>      m_myVerifyAprovalTxInfo;
    std::vector<mobj<VerifyApprovalTxInfo>> m_unknownOpinions;

    // Verification will be canceled, after any modify tx has beenbublished
    bool                            m_verificationCanceled   = false;

    //
    // Drive state
    //
    
    InfoHash                        m_rootHash;

    //
    // Request queue
    //

    std::optional<PublishedModificationApprovalTransactionInfo> m_publishedTxDuringInitialization;
    std::optional<Hash256>              m_removeDriveTx = {};
    std::optional<Hash256>              m_modificationMustBeCanceledTx;
    std::optional<CatchingUpRequest>    m_newCatchingUpRequest;
    std::deque<ModifyRequest>           m_defferedModifyRequests;
    mobj<VerificationRequest>           m_defferedVerificationRequest;

    //
    // Task variable
    //
    bool                                m_driveIsInitializing = true;
    std::optional<Hash256>              m_driveWillRemovedTx = {};
    std::optional<Hash256>              m_modificationCanceledTx;
    std::optional<CatchingUpRequest>    m_catchingUpRequest;
    std::optional<ModifyRequest>        m_modifyRequest;
    mobj<VerificationRequest>           m_verificationRequest;

    //
    // Task data
    //
    bool                                 m_taskMustBeBroken = false;

    std::optional<lt_handle>             m_downloadingLtHandle; // used for removing torrent from session

    std::set<InfoHash>                  m_catchingUpFileSet;
    std::set<InfoHash>::iterator        m_catchingUpFileIt = m_catchingUpFileSet.end();

    std::vector<uint64_t>               m_verificationCodes;

    // FsTree
    FsTree        m_fsTree;
    FsTree        m_sandboxFsTree;
    lt_handle     m_fsTreeLtHandle; // used for removing FsTree torrent from session

    // Root hashes
    InfoHash      m_sandboxRootHash;

    //
    // 'modify' opinion
    //
    std::optional<ApprovalTransactionInfo>              m_myOpinion; // (***)
    std::optional<boost::asio::high_resolution_timer>   m_shareMyOpinionTimer;
    
    // It is needed for right calculation of my 'modify' opinion
    std::optional<std::array<uint8_t,32>>       m_opinionTrafficIdentifier; // (***)
    uint64_t                                    m_expectedCumulativeDownload;
    uint64_t                                    m_accountedCumulativeDownload = 0; // (***)
    std::map<std::array<uint8_t,32>, uint64_t>  m_cumulativeUploads; // (***)
    std::map<std::array<uint8_t,32>, uint64_t>  m_lastAccountedUploads; // (***)

    // opinions from other replicators
    // key of the outer map is modification id
    // key of the inner map is a replicator key, one replicator one opinion
    std::map<Hash256, std::map<std::array<uint8_t,32>,ApprovalTransactionInfo>>    m_otherOpinions; // (***)
    
    std::optional<boost::asio::high_resolution_timer> m_modifyOpinionTimer;
    std::optional<boost::asio::high_resolution_timer> m_verifyCodeTimer;
    std::optional<boost::asio::high_resolution_timer> m_verifyOpinionTimer;

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
                  const Key&                clientPubKey,
                  size_t                    maxSize,
                  size_t                    expectedCumulativeDownload,
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
          m_client(clientPubKey),
          m_eventHandler(eventHandler),
          m_dbgEventHandler(dbgEventHandler),
          m_expectedCumulativeDownload(expectedCumulativeDownload),
          m_dbgOurPeerName(replicator.dbgReplicatorName())
    {
        m_dbgThreadId = std::this_thread::get_id();

        m_backgroundExecutor.run( [this]
        {
            initializeDrive();
        });
    }

    virtual~DefaultFlatDrive() {
    }

    const Key& drivePublicKey() const override { return m_drivePubKey; }

    void terminate() override
    {
        _ASSERT( m_dbgThreadId != std::this_thread::get_id() );
        
        //LOG_ERR ("Not fully implemented?");
        m_backgroundExecutor.stop();
        
#if 0
        std::set<lt::torrent_handle> toBeRemovedTorrents;
        toBeRemovedTorrents.insert( m_fsTreeLtHandle );

        // Add unused files into set<>
        for( const auto& [key,info] : m_torrentHandleMap )
        {
            if ( info.m_ltHandle.is_valid() )
                toBeRemovedTorrents.insert( info.m_ltHandle );
        }

        if ( !toBeRemovedTorrents.empty() )
        {
            std::promise<void> complitionPromise;
            std::future<void> complitionFuture = complitionPromise.get_future();

            // Remove unused torrents
            if ( auto session = m_session.lock(); session )
            {
                session->removeTorrentsFromSession( toBeRemovedTorrents, [&complitionPromise]
                {
                    complitionPromise.set_value();
                });
            }
            complitionFuture.wait();
        }
#endif
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

    Key getClient() const override
    {
        return m_client;
    }

    void updateReplicators(const ReplicatorList& replicators) override
    {
        DBG_MAIN_THREAD

        if (replicators.empty()) {
            _LOG_ERR( "ReplicatorList is empty!");
            return;
        }

        for (const auto& ri : replicators) {
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
        std::error_code err;
        
        if ( fs::exists(m_sandboxFsTreeFile,err) )
        {
            return fs::file_size( m_sandboxFsTreeFile, err );
        }
        return 0;
    }

    void getSandboxDriveSizes( uint64_t& metaFilesSize, uint64_t& driveSize ) const override
    {
        // TODO move to BG thread

        std::error_code err;

        if ( fs::exists(m_sandboxRootPath,err) )
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
    
    void executeOnSessionThread( const std::function<void()>& task )
    {
        if ( auto session = m_session.lock(); session )
        {
            boost::asio::post(session->lt_session().get_context(), task);
        }
    }

    // Initialize drive
    void initializeDrive()
    {
        DBG_BG_THREAD
        
        // Clear m_rootDriveHash
        memset( m_rootHash.data(), 0 , m_rootHash.size() );

        std::error_code err;
        
        // Create nonexistent folders
        if ( !fs::exists( m_fsTreeFile, err ) )
        {
            if ( !fs::exists( m_driveFolder, err ) ) {
                fs::create_directories( m_driveFolder, err );
            }

            if ( !fs::exists( m_torrentFolder, err ) ) {
                fs::create_directories( m_torrentFolder, err );
            }
        }

        // Load FsTree
        if ( fs::exists( m_fsTreeFile, err ) )
        {
            try
            {
                m_fsTree.deserialize( m_fsTreeFile );
            }
            catch( const std::exception& ex )
            {
                _LOG_ERR( "initializeDrive: m_fsTree.deserialize exception: " << ex.what() )
                fs::remove( m_fsTreeFile, err );
            }
        }
        
        // If FsTree is absent,
        // create it
        if ( !fs::exists( m_fsTreeFile, err ) )
        {
            fs::create_directories( m_fsTreeFile.parent_path(), err );
            m_fsTree.m_name = "/";
            try
            {
                m_fsTree.doSerialize( m_fsTreeFile );
            }
            catch( const std::exception& ex )
            {
                _LOG_ERR( "m_fsTree.doSerialize exception:" << ex.what() )
            }
        }

        // Calculate torrent and root hash
        m_rootHash = createTorrentFile( m_fsTreeFile,
                                        m_drivePubKey,
                                        m_fsTreeFile.parent_path(),
                                        m_fsTreeTorrent );

        // Add files to session
        addFilesToSession( m_fsTree );

        // Add FsTree to session
        if ( auto session = m_session.lock(); session )
        {
            if ( !fs::exists( m_fsTreeTorrent, err ) )
            {
                //TODO try recovery!
                _LOG_ERR( "disk corrupted: fsTreeTorrent does not exist: " << m_fsTreeTorrent )
            }
            m_fsTreeLtHandle = session->addTorrentFileToSession( m_fsTreeTorrent,
                                                                 m_fsTreeTorrent.parent_path(),
                                                                 lt::sf_is_replicator );
        }
        
        loadMyOpinion();
        loadOpinionTrafficIdentifier();
        loadAccountedCumulativeDownload();
        loadCumulativeUploads();
        loadLastAccountedUploads();
        
        if ( m_dbgEventHandler )
        {
            m_dbgEventHandler->driveIsInitialized( m_replicator, drivePublicKey(), m_rootHash );
        }

        _LOG ( "Initialized" )
        runNextTask(true);
    }

    // Recursively marks 'm_toBeRemoved' as false
    //
    void addFilesToSession( const Folder& folder )
    {
        DBG_BG_THREAD
        
        for( const auto& child : folder.m_childs )
        {
            if ( isFolder(child) )
            {
                addFilesToSession( getFolder(child) );
            }
            else
            {
                auto& hash = getFile(child).m_hash;
                std::string fileName = hashToFileName( hash );
                std::error_code err;

                if ( !fs::exists( m_driveFolder / fileName, err ) )
                {
                    //TODO inform user?
                    _LOG_ERR( "disk corrupted: drive file does not exist: " << m_driveFolder / fileName );
                }

                if ( !fs::exists( m_torrentFolder / fileName, err ) )
                {
                    //TODO try recovery
                    _LOG_ERR( "disk corrupted: torrent file does not exist: " << m_torrentFolder / fileName )
                }

                if ( auto session = m_session.lock(); session )
                {
                    auto ltHandle = session->addTorrentFileToSession( m_torrentFolder / fileName,
                                                                      m_driveFolder,
                                                                      lt::sf_is_replicator );
                    m_torrentHandleMap.try_emplace( hash, UseTorrentInfo{ltHandle,true} );
                }
            }
        }
    }

    void downgradeCumulativeUploads()
    {
        DBG_MAIN_THREAD

        _ASSERT( m_modificationCanceledTx )

        // We have already taken into account information
        // about uploads of the modification to be canceled;
        auto trafficIdentifierHasValue = m_opinionTrafficIdentifier.has_value();
        if ( !trafficIdentifierHasValue )
        {
            uint64_t sum = 0;
            for (const auto&[uploaderKey, bytes]: m_lastAccountedUploads)
            {
                sum += bytes;
                m_cumulativeUploads[uploaderKey] -= bytes;
            }
            _ASSERT( sum == m_modifyRequest->m_maxDataSize )
        }
        else
        {
            m_replicator.removeModifyDriveInfo( *m_opinionTrafficIdentifier );
            m_opinionTrafficIdentifier.reset();
        }
        m_accountedCumulativeDownload -= m_modifyRequest->m_maxDataSize;
        m_expectedCumulativeDownload = m_accountedCumulativeDownload;

        m_backgroundExecutor.run([=, this]
        {
            saveAccountedCumulativeDownload();
            if ( !trafficIdentifierHasValue )
            {
                saveCumulativeUploads();
            }
            else
            {
                saveOpinionTrafficIdentifier();
            }
            executeOnSessionThread([this] {
                continueCancelModifyDrive();
            });
        });
    }

    void runNextTask(bool runAfterInitializing = false)
    {
        m_backgroundExecutor.run([=, this]
        {
            std::error_code err;
            
            if ( !fs::exists( m_sandboxRootPath, err ) )
            {
                fs::create_directories(m_sandboxRootPath);
            }
            else
            {
                for (const auto &entry : std::filesystem::directory_iterator(m_sandboxRootPath))
                {
                    fs::remove_all( entry.path(), err );
                }
            }

            if ( !runAfterInitializing )
            {
                saveEmptyOpinion();
            }

            executeOnSessionThread([=, this] {
                runNextTaskOnMainThread(runAfterInitializing);
            });
        });
    }
    
    void runNextTaskOnMainThread(bool runAfterInitializing)
    {
        DBG_MAIN_THREAD

        if ( runAfterInitializing )
        {
            m_driveIsInitializing = false;
        }
        
        _ASSERT( !m_driveIsInitializing )

        if ( !runAfterInitializing )
        {
            m_myOpinion.reset();
        }
        
        m_removeDriveTx.reset();
        m_modificationCanceledTx.reset();
        m_catchingUpRequest.reset();
        m_modifyRequest.reset();
        
        m_isSynchronizing = false;
        //_LOG( "$$$$$$$$$$ m_isSynchronizing = false;" )
        m_taskMustBeBroken = false;

        if ( m_publishedTxDuringInitialization )
        {
            // During initializing hasBeenApprovalTransaction could be received
            // We enqueue it in 'initSynchronizeTx' since we are not able to process it at that moment
            // And now we process it
            _ASSERT( runAfterInitializing )
            auto tx = std::move(m_publishedTxDuringInitialization);
            m_publishedTxDuringInitialization.reset();
            onApprovalTransactionHasBeenPublished(*tx);
            return;
        }

        if ( m_driveWillRemovedTx )
        {
            auto tx = std::move( m_driveWillRemovedTx );
            m_driveWillRemovedTx.reset();
            runDriveClosingTask( std::move(tx) );
            return;
        }

        if( m_defferedVerificationRequest )
        {
            if ( m_defferedVerificationRequest->m_actualRootHash == m_rootHash )
            {
                m_verificationRequest = std::move( m_defferedVerificationRequest );
                runVerifyDriveTaskOnSeparateThread();

                // Verification will performed on separate thread.
                // And we should try to run other task.
                // So, do not return;
            }
        }

        if ( m_modificationMustBeCanceledTx )
        {
            auto tx = std::move( m_modificationMustBeCanceledTx );
            m_modificationMustBeCanceledTx.reset();
            runCancelModifyDriveTask( std::move(tx) );
            return;
        }
        
        if ( m_newCatchingUpRequest )
        {
            auto request = std::move( m_newCatchingUpRequest );
            m_newCatchingUpRequest.reset();
            startCatchingUpTask( std::move(request) );
            return;
        }
        
        if ( !m_defferedModifyRequests.empty() )
        {
            auto request = m_defferedModifyRequests.front();
            m_defferedModifyRequests.pop_front();
            
            startModifyDriveTask( request );
            return;
        }

    }
    
    void breakTorrentDownloadAndRunNextTask()
    {
        DBG_MAIN_THREAD

        if( m_taskMustBeBroken )
        {
            // Previous task has not been completed yet
            // So we wait for its completeness
            return;
        }

        m_taskMustBeBroken = true;

        _ASSERT(m_modifyRequest || m_catchingUpRequest)

        if ( !m_downloadingLtHandle )
        {
            // We cannot break torrent download.
            // Therefore, we will wait the end of current task, that will call runNextTask()
        }
        else
        {
            if ( auto session = m_session.lock(); session )
            {
                //
                // We must break torrent downloading because it could be unavailable
                //
                session->removeDownloadContext( *m_downloadingLtHandle );
                
                lt_handle torrentHandle = *m_downloadingLtHandle;
                m_downloadingLtHandle.reset();

                session->removeTorrentsFromSession( {torrentHandle}, [=, this]
                {
                    DBG_MAIN_THREAD
                    _LOG( "breakTorrentDownloadAndRunNextTask: torrent is removed");

                    m_backgroundExecutor.run( [this]
                    {
                        //TODO: move downloaded files from sandbox to drive (for catching-up only)

                        runNextTask();
                    });
                });
            }
        }
    }
    
    uint64_t calcHash64( uint64_t initValue, uint8_t* begin, uint8_t* end )
    {
        _ASSERT( begin < end )
        
        uint64_t hash = initValue;
        uint8_t* ptr = begin;
        
        for( ; ptr+8 <= end; ptr+=8 )
        {
            if ( hash&0x1 )
            {
                hash = (hash >> 1) | 0x8000000000000000;
            }
            else
            {
                hash = (hash >> 1);
            }

            hash ^= *reinterpret_cast<uint64_t*>(ptr);
        }
        
        uint64_t lastValue = 0;
        for( ; ptr < end; ptr++ )
        {
            lastValue |= *ptr;
            lastValue = lastValue << 8;
        }
        
        hash ^= *reinterpret_cast<uint64_t*>(ptr);

        return hash;
    }
    
    void runVerifyDriveTaskOnSeparateThread()
    {
        DBG_MAIN_THREAD

        _ASSERT( m_verificationRequest )

        //
        // At first, prepare verification on session/main thread
        //

        // Reset verification variables
        m_myVerifyCodesCalculated = false;
        m_verifyApproveTxSent   = false;
        m_verificationCanceled  = false;
        m_receivedVerifyApproveTx.reset();
        m_myVerifyAprovalTxInfo.reset();

        // add unknown opinions in 'myVerifyAprovalTxInfo'
        for( auto& opinion: m_unknownOpinions )
        {
            if ( opinion->m_tx == m_verificationRequest->m_tx.array() )
            {
                if ( !m_myVerifyAprovalTxInfo )
                {
                    m_myVerifyAprovalTxInfo = std::move(opinion);
                }
                else
                {
                    m_myVerifyAprovalTxInfo->m_opinions.push_back( opinion->m_opinions[0] );
                }
            }
        }
        // clear 'unknownOpinions'
        m_unknownOpinions.clear();

        // Add early received 'verification codes' from other replicators
        for( auto& verifyCode: m_unknownVerificationCodeQueue )
        {
            if ( verifyCode && verifyCode->m_tx == m_verificationRequest->m_tx.array() )
            {
                auto& replicatorKeys = m_verificationRequest->m_replicators;
                auto keyIt = std::find_if( replicatorKeys.begin(), replicatorKeys.end(), [&verifyCode] (const auto& it) {
                    return it.array() == verifyCode->m_replicatorKey;
                });

                if ( keyIt == replicatorKeys.end() )
                {
                    _LOG_WARN( "processVerificationCode: unknown replicatorKey" << Key(verifyCode->m_replicatorKey) );
                    return;
                }

                m_receivedVerificationCodes[verifyCode->m_replicatorKey] = std::move(verifyCode);
            }
        }

        // clear queue
        std::remove_if( m_unknownVerificationCodeQueue.begin(), m_unknownVerificationCodeQueue.end(), []( const auto& it ) {
            return !it;
        });

        //
        // Run verification task on separate thread
        //
        m_verifyThread = std::thread( [this,verificationRequest=*m_verificationRequest] () mutable
        {
            DBG_VERIFY_THREAD
            
            m_verificationCodes.clear();
            m_verificationCodes = std::vector<uint64_t>( verificationRequest.m_replicators.size(), 0 );
            
            for( uint32_t i=0; i<m_verificationCodes.size(); i++ )
            {
                uint64_t initHash = calcHash64( 0, verificationRequest.m_tx.begin(), verificationRequest.m_tx.end() );
                initHash = calcHash64( initHash, verificationRequest.m_replicators[i].begin(), verificationRequest.m_replicators[i].end() );
                m_verificationCodes[i] = initHash;
            }
            
            calculateVerifyCodes( m_fsTree );
            
            executeOnSessionThread( [this] {
                verificationCodesCompleted();
            });
        });
    }

    void calculateVerifyCodes( const Folder& folder )
    {
        DBG_VERIFY_THREAD
        
        for( const auto& child : folder.m_childs )
        {
            if ( isFolder(child) )
            {
                calculateVerifyCodes( getFolder(child) );
            }
            else
            {
                const auto& hash = getFile(child).m_hash;
                std::string fileName = m_driveFolder / toString(hash);

                std::error_code err;
                if ( !fs::exists( fileName, err ) )
                {
                    _LOG_ERR( "calculateVerifyCodes: file is absent: " << toString(hash) );
                    //TODO maybe break?
                    return;
                }
                
                uint8_t buffer[4096];
                FILE* file = fopen( fileName.c_str(), "rb" );
                
                while( !m_verificationCanceled )
                {
                    auto byteCount = fread( buffer, 1, 4096, file );
                    if ( byteCount==0 )
                        break;
                    
                    for( uint32_t i=0; i<m_verificationCodes.size(); i++ )
                    {
                        
                        uint64_t& hash = m_verificationCodes[i];
                        hash = calcHash64( hash, buffer, buffer+byteCount );
                    }
                }
            }
        }
    }

    void verificationCodesCompleted()
    {
        DBG_MAIN_THREAD
        
        m_myVerifyCodesCalculated = true;

        if ( m_receivedVerifyApproveTx || m_verificationCanceled )
        {
            // 'Verify Approval Tx' already published or canceled (we are late)
            return;
        }

        _ASSERT( m_verificationRequest );

        //
        // Get our key and verification code
        //
        const auto& replicators = m_verificationRequest->m_replicators;
        const auto& ourKey = m_replicator.replicatorKey();
        auto keyIt = std::find_if( replicators.begin(), replicators.end(), [&ourKey] (const auto& it) {
                                  return it == ourKey;
                              });
        auto ourIndex = std::distance( replicators.begin(), keyIt );
        uint64_t ourCode = m_verificationCodes[ ourIndex ];
        
        //
        // Prepare message
        //
        VerificationCodeInfo info{ m_verificationRequest->m_tx.array(), ourKey.array(), m_drivePubKey.array(), ourCode, {} };
        info.Sign( m_replicator.keyPair() );
        std::ostringstream os( std::ios::binary );
        cereal::PortableBinaryOutputArchive archive( os );
        archive( info );

        //
        // Send message to other replicators
        //
        for( auto& replicatorKey: m_verificationRequest->m_replicators )
        {
//TODO?            m_replicator.sendMessage( "code_verify", replicatorKey.array(), os.str() );
            auto it = std::find( m_replicatorList.begin(), m_replicatorList.end(), replicatorKey);
            m_replicator.sendMessage( "code_verify", it->array(), os.str() );
        }

        checkVerifyCodeNumber();
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
        }
        else
        {
            _LOG( "catchingUpIsCompleted" );
            modificationHash = m_catchingUpRequest->m_modifyTransactionHash;
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

        if ( m_driveIsInitializing )
        {
            auto it = std::find_if( m_defferedModifyRequests.begin(), m_defferedModifyRequests.end(), [&transactionHash](const auto& item)
                                { return item.m_transactionHash == transactionHash; });
            
            if ( it != m_defferedModifyRequests.end() )
            {
                m_defferedModifyRequests.erase( it );
                m_replicator.removeModifyDriveInfo( transactionHash.array() );
            }
            return;
        }
        
        _ASSERT( m_modifyRequest || m_catchingUpRequest );

        if ( m_modifyRequest && transactionHash == m_modifyRequest->m_transactionHash )
        {
            if ( m_receivedModifyApproveTx )
            {
                _LOG_ERR( "cancelModifyDrive(): m_receivedModifyApproveTx == true" )
                return;
            }

            m_modifyOpinionTimer.reset();
            
            m_modificationMustBeCanceledTx = transactionHash;
            m_modifyRequest->m_isCanceled = true;
            
            // We should break torrent downloading
            // Because they (torrents/files) may no longer be available
            //
            breakTorrentDownloadAndRunNextTask();
            return;
        }
        else
        {
            auto it = std::find_if( m_defferedModifyRequests.begin(), m_defferedModifyRequests.end(), [&transactionHash](const auto& item)
                                { return item.m_transactionHash == transactionHash; });
            
            if ( it == m_defferedModifyRequests.end() )
            {
                _LOG_ERR( "cancelModifyDrive(): invalid transactionHash: " << transactionHash );
                return;
            }
            
            m_defferedModifyRequests.erase( it );
        }
    }

    void runCancelModifyDriveTask( std::optional<Hash256>&& transactionHash )
    {
        DBG_MAIN_THREAD

        _LOG("CONTINUE CANCEL");
        
        _ASSERT( transactionHash );

        m_modificationCanceledTx = transactionHash;

        downgradeCumulativeUploads();
    }

    void continueCancelModifyDrive()
    {
        if ( m_dbgEventHandler )
        {
            m_dbgEventHandler->driveModificationIsCanceled( m_replicator, drivePublicKey(), *m_modificationCanceledTx );
        }
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
            std::error_code err;
            fs::create_directories( m_restartRootPath, err );
            if ( fs::is_directory( m_restartRootPath, err ) )
            {
                std::ofstream filestream( m_driveIsClosingPath );
                filestream << "1";
                filestream.close();
            }
        }

        if ( !m_driveIsInitializing )
        {
            if ( m_modifyRequest || m_catchingUpRequest || m_modificationCanceledTx )
            {
                breakTorrentDownloadAndRunNextTask();
            }
            else
            {
                runNextTask();
            }
        }
    }

    void onVerifyApprovalTransactionHasBeenPublished( PublishedVerificationApprovalTransactionInfo info ) override
    {
        DBG_MAIN_THREAD

        m_receivedVerifyApproveTx = info.m_tx;
        m_verifyCodeTimer.reset();
        m_verificationRequest.reset();
    }

    void processVerificationCode( mobj<VerificationCodeInfo>&& info ) override
    {
        DBG_MAIN_THREAD

        // Save verification opinion in queue, if we so far does not received verificationRequest
        if ( !m_verificationRequest || info->m_tx != m_verificationRequest->m_tx.array() )
        {
            if ( m_verificationRequest )
            {
                _LOG( "processVerificationCode: m_verificationRequest->m_tx: " << m_verificationRequest->m_tx )
            }
            _LOG( "processVerificationCode: unknown tx: " << Key(info->m_tx) )

            m_unknownVerificationCodeQueue.emplace_back( std::move(info) );
            return;
        }

        //
        // Check replicator key, it must be in replicatorList
        //
        auto& replicatorKeys = m_verificationRequest->m_replicators;
        auto keyIt = std::find_if( replicatorKeys.begin(), replicatorKeys.end(), [&key=info->m_replicatorKey] (const auto& it) {
            return it.array() == key;
        });

        if ( keyIt == replicatorKeys.end() )
        {
            _LOG_WARN( "processVerificationCode: unknown replicatorKey" << Key(info->m_replicatorKey) );
        }

        m_receivedVerificationCodes[info->m_replicatorKey] = std::move(info);

        if ( m_myVerifyCodesCalculated )
        {
            checkVerifyCodeNumber();
        }
    }

    void processVerificationOpinion( mobj<VerifyApprovalTxInfo>&& info ) override
    {
        DBG_MAIN_THREAD

        _ASSERT( info->m_opinions.size() == 1 )
        
        if ( m_receivedVerifyApproveTx == info->m_tx )
        {
            _LOG( "processVerificationOpinion: verificaty approval tx already published: " << Hash256(info->m_tx) )
            return;
        }

        if ( !m_verificationRequest || m_verificationRequest->m_tx != info->m_tx )
        {
            // opinion is old or new
            _LOG( "processVerificationOpinion: received unknown verification opinion: " << Hash256(info->m_tx) )
            m_unknownOpinions.emplace_back( std::move(info) );
            return;
        }

        if ( !m_myVerifyAprovalTxInfo )
        {
            m_myVerifyAprovalTxInfo = std::move(info);
            return;
        }

        _ASSERT( m_myVerifyAprovalTxInfo->m_tx == info->m_tx )

        if ( m_myVerifyAprovalTxInfo->m_shardId != info->m_shardId )
        {
            _LOG_WARN( "processVerificationOpinion: received opinion with different m_shardId: " << info->m_shardId << " vs " << m_myVerifyAprovalTxInfo->m_shardId )
            return;
        }

        // At any case remove opinions with the same replicator key must be removed
        //
        auto& opinions = m_myVerifyAprovalTxInfo->m_opinions;
        std::remove_if( opinions.begin(), opinions.end(), [&info] (const auto& it) {
            return it.m_publicKey == info->m_opinions[0].m_publicKey;
        });

        // Check sender replicator key
        //
        const auto& keys = m_verificationRequest->m_replicators;
        bool isUnexpected = std::find_if( keys.begin(), keys.end(), [&info] (const auto& it) {
            return it.array() == info->m_opinions[0].m_publicKey;
        }) == keys.end();

        if ( isUnexpected )
        {
            _LOG_WARN( "processVerificationOpinion: received opinion from unexpected replicator: " << Key(info->m_opinions[0].m_publicKey) )
            return;
        }

        m_myVerifyAprovalTxInfo->m_opinions.emplace_back( info->m_opinions[0] );

        if ( m_myVerifyAprovalTxInfo->m_opinions.size() == m_verificationRequest->m_replicators.size() )
        {
            verifyOpinionTimerExpired();
        }
        else if ( m_myVerifyAprovalTxInfo->m_opinions.size() > (m_verificationRequest->m_replicators.size() *2)/3 )
        {
            // start timer if it is not started
            if ( !m_verifyOpinionTimer )
            {
                if ( auto session = m_session.lock(); session )
                {
                    m_verifyCodeTimer = session->startTimer( m_replicator.getVerifyCodeTimerDelay(),
                                        [this]() { verifyOpinionTimerExpired(); } );
                }
            }
        }
    }

    void verifyOpinionTimerExpired()
    {
        m_verifyApproveTxSent = true;
        m_verifyCodeTimer.reset();
        m_verifyOpinionTimer.reset();

        m_eventHandler.verificationTransactionIsReady( m_replicator, *m_myVerifyAprovalTxInfo );
    }

    void checkVerifyCodeNumber()
    {
        DBG_MAIN_THREAD

        _ASSERT( !m_receivedVerifyApproveTx );
        _ASSERT( m_verificationRequest );

        auto replicatorNumber = m_verificationRequest->m_replicators.size();

        // check code number
        if ( m_myVerifyCodesCalculated && m_receivedVerificationCodes.size() == replicatorNumber-1 )
        {
            m_verifyCodeTimer.reset();
            verifyCodeTimerExpired();
        }
        else if ( !m_verifyApproveTxSent && !m_receivedVerifyApproveTx )
        {
            // start timer if it is not started
            if ( !m_verifyCodeTimer )
            {
                if ( auto session = m_session.lock(); session )
                {
                    m_verifyCodeTimer = session->startTimer( m_replicator.getVerifyCodeTimerDelay(),
                                        [this]() { verifyCodeTimerExpired(); } );
                }
            }
        }
    }

    void verifyCodeTimerExpired()
    {
        DBG_MAIN_THREAD

        if ( m_verifyApproveTxSent || m_receivedVerifyApproveTx )
            return;

        _ASSERT( m_verificationRequest );

        // Prepare 'Verify Approval Tx Info'
        VerifyApprovalTxInfo info {
                                        m_verificationRequest->m_tx.array(),
                                        m_drivePubKey.array(),
                                        m_verificationRequest->m_shardId,
            {} };

        VerifyApprovalInfo myOpinion = { m_replicator.replicatorKey().array(), {}, {}, {} };

        auto& keyList = m_verificationRequest->m_replicators;
        myOpinion.m_opinionKeys.reserve( keyList.size() );
        myOpinion.m_opinions.reserve( keyList.size() );

        for( size_t i=0; i<keyList.size(); i++ )
        {
            auto& key = keyList[i].array();

            myOpinion.m_opinionKeys[i] = key;

            if ( key == m_replicator.replicatorKey().array() )
            {
                myOpinion.m_opinions[i] = 1;
            }
            else
            {
                if ( auto verifyInfoIt = m_receivedVerificationCodes.find(key); verifyInfoIt != m_receivedVerificationCodes.end() )
                {
                    myOpinion.m_opinions[i] = (verifyInfoIt->second->m_code == m_verificationCodes[i]);
                }
                else
                {
                    myOpinion.m_opinions[i] = 1;
                }
            }
        }

        myOpinion.Sign( m_replicator.keyPair(), m_verificationRequest->m_tx.array(), m_drivePubKey.array(), m_verificationRequest->m_shardId );

        info.m_opinions.push_back( myOpinion );
        processVerificationOpinion( {info} );

        std::ostringstream os( std::ios::binary );
        cereal::PortableBinaryOutputArchive archive( os );
        archive( info );

        for( auto& replicatorKey: m_verificationRequest->m_replicators )
        {
//TODO?            m_replicator.sendMessage( "code_verify", replicatorKey.array(), os.str() );
            auto it = std::find( m_replicatorList.begin(), m_replicatorList.end(), replicatorKey);
            m_replicator.sendMessage( "verify_opinion", it->array(), os.str() );
        }
    }

//    void checkVerifyOpinionNumberAndStartTimer()
//    {
//        DBG_MAIN_THREAD
//
//        _ASSERT( m_verificationRequest );
//
//        auto replicatorNumber = m_verificationRequest->m_replicators.size() -1; //TODO remove -1;
//
//        // check opinion number
//        if ( m_verifyApprovalInfo.m_opinions.size() >= ((replicatorNumber)*2)/3
//            && !m_verifyApproveTxSent && !m_receivedVerifyApproveTx )
//        {
//            // start timer if it is not started
//            if ( !m_verifyCodeTimer )
//            {
//                if ( auto session = m_session.lock(); session )
//                {
//                    m_verifyCodeTimer = session->startTimer( m_replicator.getVerifyApprovalTransactionTimerDelay(),
//                                        [this]() { verifyCodeTimerExpired(); } );
//                }
//            }
//        }
//    }
    
    void cancelVerification( mobj<Hash256>&& tx ) override
    {
        DBG_MAIN_THREAD

        if ( !m_verificationRequest )
        {
            _LOG_ERR( "cancelVerification: internal error: m_verificationRequest == null" )
            return;
        }
        
        if ( tx->array() != m_verificationRequest->m_tx )
        {
            _LOG_ERR( "cancelVerification: internal error: bad tx:" << tx )
            return;
        }

        m_verificationCanceled = true;

        if ( m_verifyThread.joinable() )
        {
            try {
                m_verifyThread.join();
            }
            catch(...) {
            }
        }
    }


    void startVerification( mobj<VerificationRequest>&& request ) override
    {
        DBG_MAIN_THREAD

        m_defferedVerificationRequest = std::move(request);

        if ( m_driveIsInitializing )
        {
            return;
        }

        if ( m_verificationRequest )
        {
            _LOG_ERR("startVerification: internal error: m_verificationRequest != null")
        }
        
        if ( m_defferedVerificationRequest->m_actualRootHash == m_rootHash )
        {
            if ( m_isSynchronizing )
            {
                // Outdated request root hash (not properly extracted rootHash from blockchain)
                _LOG_ERR( "startVerification: m_isSynchronizing must be false" )
            }
            _ASSERT( !m_isSynchronizing )
            m_verificationRequest = std::move( m_defferedVerificationRequest );
            runVerifyDriveTaskOnSeparateThread();
        }
    }

    void runDriveClosingTask( std::optional<Hash256>&& transactionHash )
    {
        DBG_MAIN_THREAD
        
        _ASSERT( transactionHash );
        _ASSERT( !m_removeDriveTx );

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

        if ( auto session = m_session.lock(); session )
        {
            session->removeTorrentsFromSession( tobeRemovedTorrents, [this](){
                m_replicator.closeDriveChannels( *m_removeDriveTx, *this );
            });
        }
    }

    void synchronizeDriveWithSandbox()
    {
        DBG_MAIN_THREAD

        if ( m_newCatchingUpRequest || m_modificationMustBeCanceledTx || m_removeDriveTx )
        {
            runNextTask();
            return;
        }
        
        _ASSERT( m_sandboxCalculated );
        _ASSERT( !m_modifyRequest->m_isCanceled );
        _ASSERT( m_modifyRequest || m_catchingUpRequest );

        m_isSynchronizing = true;
        //_LOG( "$$$$$$$$$$ m_isSynchronizing = true;" )

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
        
        m_replicatorList = modifyRequest.m_replicators;

        // ModificationIsCanceling check is redundant now
        if ( m_modifyRequest || m_catchingUpRequest || m_newCatchingUpRequest ||
             m_modificationCanceledTx || m_driveIsInitializing )
        {
            _LOG( "startModifyDrive: queue modifyRequest" );
            m_defferedModifyRequests.emplace_back( std::move(modifyRequest) );
            return;
        }
        startModifyDriveTask( modifyRequest );
    }
    
    void startModifyDriveTask( const ModifyRequest& modifyRequest )
    {
        m_modifyUserDataReceived       = false;
        m_sandboxCalculated            = false;
        m_modifyApproveTransactionSent = false;
        m_receivedModifyApproveTx.reset();

        m_modifyRequest = modifyRequest;

        using namespace std::placeholders;  // for _1, _2, _3

        m_expectedCumulativeDownload += m_modifyRequest->m_maxDataSize;

        _ASSERT( !m_opinionTrafficIdentifier );

        _LOG( "started modification" )

        m_opinionTrafficIdentifier = m_modifyRequest->m_transactionHash.array();
        
        if ( auto session = m_session.lock(); session )
        {
            _ASSERT( m_opinionTrafficIdentifier )
            m_downloadingLtHandle = session->download( DownloadContext(
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
    
        _ASSERT( m_modifyRequest )
        _ASSERT( m_modifyRequest->m_clientDataInfoHash == infoHash )

        if ( code == download_status::failed )
        {
            if ( m_dbgEventHandler )
            {
                m_dbgEventHandler->modifyTransactionEndedWithError( m_replicator, m_drivePubKey, *m_modifyRequest, errorText, 0 );
            }
            modifyIsCompleted();
            return;
        }

        if ( code == download_status::download_complete )
        {
            _ASSERT( !m_taskMustBeBroken );

            m_downloadingLtHandle.reset();

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
        DBG_BG_THREAD

        _ASSERT( m_modifyRequest.has_value() != m_catchingUpRequest.has_value() )

        // There are 2 cases:
        //  - modify
        //  - catchingUp
        //
        if ( m_modifyRequest )
        {
            std::error_code err;
            
            // Check that client data exist
            if ( !fs::exists(m_clientDataFolder,err) || !fs::is_directory(m_clientDataFolder,err) )
            {
                _LOG_ERR( "modifyDriveInSandbox: 'client-data' is absent; m_clientDataFolder=" << m_clientDataFolder );
                if ( m_dbgEventHandler )
                {
                    m_dbgEventHandler->modifyTransactionEndedWithError( m_replicator, m_drivePubKey, *m_modifyRequest, "modify drive: 'client-data' is absent", -1 );
                }
                executeOnSessionThread( [=,this]
                {
                    modifyIsCompleted();
                });
                return;
            }

            // Check 'actionList.bin' is received
            if ( !fs::exists( m_clientActionListFile, err ) )
            {
                _LOG_ERR( "modifyDriveInSandbox: 'ActionList.bin' is absent: " << m_clientActionListFile );
                if ( m_dbgEventHandler )
                {
                    m_dbgEventHandler->modifyTransactionEndedWithError( m_replicator, m_drivePubKey, *m_modifyRequest, "modify drive: 'ActionList.bin' is absent", -1 );
                }
                executeOnSessionThread( [=,this]()
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
                        if ( !fs::exists( clientFile, err ) || fs::is_directory( clientFile, err ) )
                        {
                            _LOG( "! ActionList: invalid 'upload': file/folder not exists: " << clientFile )
                            action.m_isInvalid = true;
                            break;
                        }

                        try
                        {
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

                            _LOG( "ActionList: successful 'upload': " << clientFile )
                        }
                        catch( const std::exception& error )
                        {
                            _LOG_ERR( "ActionList: exception during 'upload': " << clientFile << "; " << error.what() )
                        }
                        catch(...)
                        {
                            _LOG_ERR( "ActionList: unknown exception during 'upload': " << clientFile )
                        }
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
                            _LOG( "! ActionList: invalid 'new_folder': such entry already exists: " << action.m_param1 )
                            action.m_isInvalid = true;
                            break;
                        }

                        m_sandboxFsTree.addFolder( action.m_param1 );

                        _LOG( "ActionList: successful 'new_folder': " << action.m_param1 )
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
                            _LOG( "! ActionList: invalid 'move': 'srcPath' not exists (in FsTree): " << action.m_param1 )
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
                                _LOG( "! ActionList: invalid 'move': 'srcPath' is a directory which is an ancestor of 'destPath'" )
                                _LOG( "  invalid 'move': 'srcPath' : " << action.m_param1  );
                                _LOG( "  invalid 'move': 'destPath' : " << action.m_param2  );
                                action.m_isInvalid = true;
                                break;
                            }
                        }

                        // modify FsTree
                        m_sandboxFsTree.moveFlat( action.m_param1, action.m_param2, [/*this*/] ( const InfoHash& /*fileHash*/ )
                        {
                            //m_torrentMap.try_emplace( fileHash, UseTorrentInfo{} );
                        } );

                        _LOG( "ActionList: successful 'move': "  << action.m_param1 << " -> " << action.m_param2 )
                        break;
                    }
                    //
                    // Remove
                    //
                    case action_list_id::remove: {

                        if ( m_sandboxFsTree.getEntryPtr( action.m_param1 ) == nullptr )
                        {
                            _LOG( "! ActionList: invalid 'remove': 'srcPath' not exists (in FsTree): " << action.m_param1  );
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
                        
                        _LOG( "! ActionList: successful 'remove': " << action.m_param1  );
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
                try {
                    fs::rename(  m_sandboxRootPath / fileName, m_driveFolder / fileName );
                }
                catch ( const std::exception& ex ) {
                    _LOG( "exception during rename:" << ex.what() );
                    _LOG_ERR( "exception during rename '" << m_sandboxRootPath / fileName <<
                              "' to '" << m_driveFolder / fileName << "'; " << ex.what() );
                }

                // create torrent
                calculateInfoHashAndCreateTorrentFile( m_driveFolder / fileName, m_drivePubKey, m_torrentFolder, "" );
            }
        }
        m_sandboxRootHash = createTorrentFile( m_sandboxFsTreeFile,
                                               m_drivePubKey,
                                               m_sandboxRootPath,
                                               m_sandboxFsTreeTorrent );

        executeOnSessionThread( [=,this]()
        {
            myRootHashIsCalculated();
        });
    }

    void normalizeUploads(std::map<std::array<uint8_t,32>, uint64_t>& modificationUploads, uint64_t targetSum)
    {
        uint128_t longTargetSum = targetSum;
        uint128_t sumBefore = std::accumulate(modificationUploads.begin(),
                                              modificationUploads.end(),
                                              0,
                                              [] (const uint64_t& value, const std::pair<Key, int>& p)
                                              { return value + p.second; }
                                              );
        
        uint64_t sumAfter = 0;

        if ( sumBefore > 0 )
        {
            for ( auto& [key, uploadBytes]: modificationUploads ) {
                if ( key != m_modifyRequest->m_clientPublicKey.array() )
                {
                    auto longUploadBytes = (uploadBytes * longTargetSum) / sumBefore;
                    uploadBytes = longUploadBytes.convert_to<uint64_t>();
                    sumAfter += uploadBytes;
                }
            }
            modificationUploads[m_modifyRequest->m_clientPublicKey.array()] = targetSum - sumAfter;
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
            if (auto it = modifyTrafficMap.find(replicatorIt.array());
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
        m_replicator.removeModifyDriveInfo( *m_opinionTrafficIdentifier );
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
        _ASSERT( !m_myOpinion )
        _ASSERT( m_modifyRequest.has_value() != m_catchingUpRequest.has_value() )
        _ASSERT( !m_modificationCanceledTx )

        updateCumulativeUploads();

        //
        // Calculate upload opinion
        //
        SingleOpinion opinion( m_replicator.replicatorKey().array() );
        for( const auto& replicatorIt : m_replicatorList )
        {
            auto it = m_cumulativeUploads.find( replicatorIt.array() );
            opinion.m_uploadLayout.push_back( {replicatorIt.array(), it->second} );
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

        m_backgroundExecutor.run([this] {
            saveMyOpinion();
            saveOpinionTrafficIdentifier();
            saveCumulativeUploads();
            saveLastAccountedUploads();
            saveAccountedCumulativeDownload();
            executeOnSessionThread([this] {
                myOpinionIsCreated();
            });
        });
    }

    void myRootHashIsCalculated()
    {
        DBG_MAIN_THREAD

        _ASSERT( m_modifyRequest.has_value() != m_catchingUpRequest.has_value() )
        
        if ( m_newCatchingUpRequest || m_modificationMustBeCanceledTx || m_removeDriveTx )
        {
            runNextTask();
            return;
        }

        if ( strcmp(m_dbgOurPeerName, "replicator_04") == 0 )
        {
            std::cout << "ready" << std::endl;
        }

        // Notify
        if ( m_dbgEventHandler )
            m_dbgEventHandler->rootHashIsCalculated( m_replicator, m_drivePubKey, m_modifyRequest->m_transactionHash, m_sandboxRootHash );
        
        // Calculate my opinion
        createMyOpinion();
    }

    void myOpinionIsCreated()
    {
        DBG_MAIN_THREAD

        _ASSERT( m_modifyRequest.has_value() != m_catchingUpRequest.has_value() )
        _ASSERT( m_myOpinion )

        if ( m_newCatchingUpRequest || m_modificationMustBeCanceledTx || m_removeDriveTx )
        {
            runNextTask();
            return;
        }

        m_sandboxCalculated = true;

        if ( m_receivedModifyApproveTx )
        {
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
            if ( auto session = m_session.lock(); session )
            {
                m_shareMyOpinionTimer = session->startTimer(1000 * 60 * 2, [this] {
                    shareMyOpinion();
                });
            }

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
        
        for( const auto& replicatorIt : m_modifyRequest->m_replicators )
        {
            m_replicator.sendMessage( "opinion", replicatorIt.array(), os.str() );
        }
    }
    
    void checkOpinionNumberAndStartTimer()
    {
        DBG_MAIN_THREAD
        
        // m_replicatorList is the list of other replicators (it does not contain our replicator)
        auto replicatorNumber = m_modifyRequest->m_replicators.size();//todo++++ +1;

        // check opinion number
        if ( m_myOpinion && m_otherOpinions[m_modifyRequest->m_transactionHash].size() >= ((replicatorNumber)*2)/3
            && !m_modifyApproveTransactionSent && !m_receivedModifyApproveTx )
        {
            // start timer if it is not started
            if ( !m_modifyOpinionTimer )
            {
                if ( auto session = m_session.lock(); session )
                {
                    m_modifyOpinionTimer = session->startTimer( m_replicator.getModifyApprovalTransactionTimerDelay(),
                                        [this]() { opinionTimerExpired(); } );
                }
            }
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
        if ( auto session = m_session.lock(); session )
        {
            session->removeTorrentsFromSession( toBeRemovedTorrents, [this, nextStep]
            {
                m_backgroundExecutor.run( [nextStep]
                {
                    nextStep();
                });
            });
        }
    }

    // updates drive (2st phase after fsTree torrent removed)
    // - remove unused files and torrent files
    // - add new torrents to session
    //
    void updateDrive_2()
    {
        DBG_BG_THREAD
        
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
                    if ( auto session = m_session.lock(); session )
                    {
                        std::string fileName = hashToFileName( it.first );
                        it.second.m_ltHandle = session->addTorrentFileToSession( m_torrentFolder / fileName,
                                                                                   m_driveFolder,
                                                                                   lt::sf_is_replicator );
                    }
                }
            }

            // Add FsTree torrent to session
            _LOG( "Add FsTree torrent to session: " << toString(m_rootHash) );
            if ( auto session = m_session.lock(); session )
            {
                m_fsTreeLtHandle = session->addTorrentFileToSession( m_fsTreeTorrent,
                                                                     m_fsTreeTorrent.parent_path(),
                                                                     lt::sf_is_replicator );
            }

            executeOnSessionThread( [=,this]
            {
                modifyIsCompleted();
            });
        }
        catch ( const std::exception& ex )
        {
            _LOG_ERR( "exception during updateDrive_2: " << ex.what() );
            runNextTask();
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
        m_eventHandler.modifyApprovalTransactionIsReady( m_replicator, std::move(info) );

        m_modifyApproveTransactionSent = true;
    }

    void onApprovalTransactionHasBeenPublished( const PublishedModificationApprovalTransactionInfo& transaction ) override
    {
        DBG_MAIN_THREAD

        if ( m_driveIsInitializing )
        {
            m_publishedTxDuringInitialization = transaction;
            return;
        }

        if ( m_receivedModifyApproveTx &&  *m_receivedModifyApproveTx == transaction.m_modifyTransactionHash )
        {
            _LOG_ERR("Duplicated approval tx ");
            return;
        }

        m_receivedModifyApproveTx = transaction.m_modifyTransactionHash;

        if ( m_verificationRequest )
        {
            cancelVerification( m_verificationRequest->m_tx );
        }

        // stop timer
        m_modifyOpinionTimer.reset();
        m_shareMyOpinionTimer.reset();
        m_otherOpinions.erase(transaction.m_modifyTransactionHash);

        if ( m_modificationCanceledTx || m_modificationMustBeCanceledTx )
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

        if ( m_rootHash == transaction.m_rootHash)
        {
            if ( m_myOpinion )
            {
                sendSingleApprovalTransaction();
            }
            runNextTask();
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

            if ( m_modifyRequest || m_catchingUpRequest )
            {
                // We should break torrent downloading
                // Because they (torrents/files) may no longer be available
                //
                breakTorrentDownloadAndRunNextTask();
            }
            else
            {
                runNextTask();
            }
            return;
        }

        if ( !m_sandboxCalculated )
        {
            // wait the end of sandbox calculation
            return;
        }
        else
        {
            const auto& v = transaction.m_replicatorKeys;
            auto it = std::find( v.begin(), v.end(), m_replicator.replicatorKey().array());

            // Is my opinion present
            if ( it == v.end() )
            {
                // Send Single Approval Transaction At First
                sendSingleApprovalTransaction();
            }

            synchronizeDriveWithSandbox();
        }
    }

    void onApprovalTransactionHasFailedInvalidOpinions(const Hash256 &transactionHash) override
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

        _ASSERT( m_myOpinion )

        auto copy = *m_myOpinion;
        m_eventHandler.singleModifyApprovalTransactionIsReady( m_replicator, std::move(copy) );
    }

    void onSingleApprovalTransactionHasBeenPublished( const PublishedModificationSingleApprovalTransactionInfo& transaction ) override
    {
        m_verificationCanceled = true;
        _LOG( "onSingleApprovalTransactionHasBeenPublished()" );
    }

    void startCatchingUpTask( std::optional<CatchingUpRequest>&& actualCatchingRequest )
    {
        DBG_MAIN_THREAD
        
        _ASSERT( !m_modificationCanceledTx );
        _ASSERT( !m_modifyRequest );
        _ASSERT( !m_removeDriveTx );

        _ASSERT( actualCatchingRequest )

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
            while ( !m_defferedModifyRequests.empty() and it != m_defferedModifyRequests.begin() )
            {
                if ( !m_opinionTrafficIdentifier
                     || *m_opinionTrafficIdentifier != m_defferedModifyRequests.front().m_transactionHash.array() )
                {
                    m_replicator.removeModifyDriveInfo(m_defferedModifyRequests.front().m_transactionHash.array());
                }
                m_defferedModifyRequests.pop_front();
            }
            m_expectedCumulativeDownload += m_defferedModifyRequests.front().m_maxDataSize;
            m_defferedModifyRequests.pop_front();
            if ( m_opinionTrafficIdentifier &&
                *m_opinionTrafficIdentifier != m_defferedModifyRequests.front().m_transactionHash.array() )
            {
                m_replicator.removeModifyDriveInfo(m_defferedModifyRequests.front().m_transactionHash.array());
            }
        }

        m_catchingUpRequest = std::move( actualCatchingRequest );
        
        //
        // Start download fsTree
        //
        using namespace std::placeholders;  // for _1, _2, _3

        _LOG( "Late: download FsTree:" << m_catchingUpRequest->m_rootHash )
        
        if ( !m_opinionTrafficIdentifier )
        {
            m_opinionTrafficIdentifier = m_catchingUpRequest->m_modifyTransactionHash.array();
        }

        if ( auto session = m_session.lock(); session )
        {
            _ASSERT( m_opinionTrafficIdentifier )
            m_downloadingLtHandle = session->download( DownloadContext(
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
            _ASSERT( !m_taskMustBeBroken );

            m_downloadingLtHandle.reset();

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
        }
        catch(...)
        {
            _LOG_ERR( "cannot deserialize 'CatchingUpFsTree'" );
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
                std::error_code err;
                
                if ( !fs::exists( m_driveFolder / toString(hash), err ) )
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
        
        _ASSERT( !m_taskMustBeBroken );

        if ( m_catchingUpFileIt == m_catchingUpFileSet.end() )
        {
            m_downloadingLtHandle.reset();
            
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
                _LOG_ERR( "Not Implemented" );
                return;
            }

            auto missingFileHash = *m_catchingUpFileIt;
            m_catchingUpFileIt++;

            if ( auto session = m_session.lock(); session )
            {
                _ASSERT( m_opinionTrafficIdentifier )
                m_downloadingLtHandle = session->download( DownloadContext(
                                                                         
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
                                                         _LOG_ERR("???");
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
    }
    
    void completeCatchingUp()
    {
        DBG_BG_THREAD
        
        try
        {
            //
            // Check RootHash Before All
            //
            _LOG("m_sandboxRootHash: " << m_sandboxRootHash );
            _LOG("m_catchingUpRootHash: " << m_catchingUpRequest->m_rootHash );
            _ASSERT( m_sandboxRootHash == m_catchingUpRequest->m_rootHash );

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
                if ( auto session = m_session.lock(); session )
                {
                    auto tHandle = session->addTorrentFileToSession( m_torrentFolder / fileName,
                                                                       m_driveFolder,
                                                                       lt::sf_is_replicator );
                    _ASSERT( tHandle.is_valid() );
                    m_torrentHandleMap.try_emplace( fileHash, UseTorrentInfo{tHandle,true} );
                }
            }

            // Add FsTree torrent to session
            if ( auto session = m_session.lock(); session )
            {
                m_fsTreeLtHandle = session->addTorrentFileToSession( m_fsTreeTorrent,
                                                                       m_fsTreeTorrent.parent_path(),
                                                                       lt::sf_is_replicator );
            }

            // remove unused data from 'torrentMap'
            std::erase_if( m_torrentHandleMap, [] (const auto& it) { return !it.second.m_isUsed; } );

            LOG( "drive is synchronized" );

            executeOnSessionThread( [=,this]
            {
                modifyIsCompleted();
            });
        }
        catch ( const std::exception& ex )
        {
            _LOG_ERR( "exception during completeCatchingUp: " << ex.what() );
            runNextTask();
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
        _LOG ( "In Remove all drive data" );
        m_backgroundExecutor.run( [this]
        {
            DBG_BG_THREAD

            try {
                // remove drive root folder and sandbox
                fs::remove_all( m_sandboxRootPath );
                fs::remove_all( m_driveRootPath );
            }
            catch ( const std::exception& ex )
            {
                _LOG_ERR( "exception during removeAllDriveData: " << ex.what() );
                runNextTask();
            }

            if ( m_dbgEventHandler )
            {
                m_dbgEventHandler->driveIsClosed( m_replicator, m_drivePubKey, *m_removeDriveTx );
            }

            if ( auto session = m_session.lock(); session )
            {
                boost::asio::post(session->lt_session().get_context(), [this] {
                    m_replicator.finishDriveClosure( drivePublicKey() );
                });
            }
        });
    }

    const ReplicatorList&  replicatorList() const override
    {
        return m_replicatorList;
    }

    void printDriveStatus() override
    {
        LOG("Drive Status:")
        m_fsTree.dbgPrint();
        if ( auto session = m_session.lock(); session )
        {
            session->printActiveTorrents();
        }
    }
    
    
    //-----------------------------------------------------------------------------

    // m_myOpinion
    void saveEmptyOpinion()
    {
        std::optional<ApprovalTransactionInfo> opinion;
        saveRestartValue( opinion, "myOpinion" );
    }
    void saveMyOpinion()
    {
        saveRestartValue( m_myOpinion, "myOpinion" );
    }
    void loadMyOpinion()
    {
        loadRestartValue( m_myOpinion, "myOpinion" );
    }
    
    // m_opinionTrafficIdentifier
    void saveOpinionTrafficIdentifier()
    {
        saveRestartValue( m_opinionTrafficIdentifier, "opinionTrafficIdentifier" );
    }
    void loadOpinionTrafficIdentifier()
    {
        loadRestartValue( m_opinionTrafficIdentifier, "opinionTrafficIdentifier" );
    }
    
    // m_accountedCumulativeDownload
    void saveAccountedCumulativeDownload()
    {
        saveRestartValue( m_accountedCumulativeDownload, "accountedCumulativeDownload" );
    }
    void loadAccountedCumulativeDownload()
    {
        if ( !loadRestartValue( m_accountedCumulativeDownload, "accountedCumulativeDownload" ) )
        {
            m_accountedCumulativeDownload = 0;
        }
    }

    // m_cumulativeUploads
    void saveCumulativeUploads()
    {
        saveRestartValue( m_cumulativeUploads, "cumulativeUploads" );
    }
    void loadCumulativeUploads()
    {
        loadRestartValue( m_cumulativeUploads, "cumulativeUploads" );
    }

    // m_lastAccountedUploads
    void saveLastAccountedUploads()
    {
        saveRestartValue( m_lastAccountedUploads, "lastAccountedUploads" );
    }
    void loadLastAccountedUploads()
    {
        loadRestartValue(  m_lastAccountedUploads, "lastAccountedUploads" );
    }

    template<class T>
    void saveRestartValue( T& value, std::string path )
    {
        std::ostringstream os( std::ios::binary );
        cereal::PortableBinaryOutputArchive archive( os );
        archive( value );
        
        saveRestartData( m_restartRootPath / path , os.str() );
    }
    
    template<class T>
    bool loadRestartValue( T& value, std::string path )
    {
        std::string data;
        
        if ( !loadRestartData( m_restartRootPath / path, data ) )
        {
            return false;
        }
        
        std::istringstream is( data, std::ios::binary );
        cereal::PortableBinaryInputArchive iarchive(is);
        iarchive( value );
        return true;
    }
    
    void saveRestartData( std::string outputFile, const std::string data )
    {
        try
        {
            {
                std::ofstream fStream( outputFile +".tmp", std::ios::binary );
                fStream << data;
            }
            std::error_code err;
            fs::remove( outputFile, err );
            fs::rename( outputFile +".tmp", outputFile , err );
        }
        catch( const std::exception& ex)
        {
            _LOG_ERR( "exception during saveRestartData: " << ex.what() );
        }
    }
    
    bool loadRestartData( std::string outputFile, std::string& data )
    {
        std::error_code err;

        if ( fs::exists( outputFile, err ) )
        {
            std::ifstream ifStream( outputFile, std::ios::binary );
            if ( ifStream.is_open() )
            {
                std::ostringstream os;
                os << ifStream.rdbuf();
                data = os.str();
                return true;
            }
        }
        
        if ( fs::exists( outputFile +".tmp", err ) )
        {
            std::ifstream ifStream( outputFile +".tmp", std::ios::binary );
            if ( ifStream.is_open() )
            {
                std::ostringstream os;
                os << ifStream.rdbuf();
                data = os.str();
                return true;
            }
        }
        
        //_LOG_WARN( "cannot loadRestartData: " << outputFile );
        
        return false;
    }
};


std::shared_ptr<FlatDrive> createDefaultFlatDrive(
        std::shared_ptr<Session> session,
        const std::string&       replicatorRootFolder,
        const std::string&       replicatorSandboxRootFolder,
        const Key&               drivePubKey,
        const Key&               clientPubKey,
        size_t                   maxSize,
        size_t                   usedDriveSizeExcludingMetafiles,
        ReplicatorEventHandler&  eventHandler,
        Replicator&              replicator,
        const std::vector<Key>&    replicators,
        DbgReplicatorEventHandler* dbgEventHandler )

{
    return std::make_shared<DefaultFlatDrive>( session,
                                           replicatorRootFolder,
                                           replicatorSandboxRootFolder,
                                           drivePubKey,
                                           clientPubKey,
                                           maxSize,
                                           usedDriveSizeExcludingMetafiles,
                                           eventHandler,
                                           replicator,
                                           replicators,
                                           dbgEventHandler );
}

}

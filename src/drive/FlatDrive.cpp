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
#include "drive/FlatDrivePaths.h"
#include "drive/Replicator.h"
#include "drive/Session.h"
#include "drive/BackgroundExecutor.h"
#include "drive/ActionList.h"
#include "drive/DriveTask.h"
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

class RestartValueSerializer
{

public:

    fs::path            m_restartRootPath;

    std::thread::id     m_dbgThreadId;
    std::string         m_dbgOurPeerName;

    RestartValueSerializer(
            const fs::path& restartRootPath,
            const std::string& dbgOurPeerName )
            : m_restartRootPath( restartRootPath )
            , m_dbgThreadId( std::this_thread::get_id())
            , m_dbgOurPeerName( dbgOurPeerName )
    {}

    template<class T>
    void saveRestartValue( T& value, std::string path ) const
    {
        DBG_BG_THREAD

        _ASSERT( fs::exists( m_restartRootPath ))

        std::ostringstream os( std::ios::binary );
        cereal::PortableBinaryOutputArchive archive( os );
        archive( value );

        saveRestartData( m_restartRootPath / path, os.str());
    }

    template<class T>
    bool loadRestartValue( T& value, std::string path ) const
    {
        DBG_BG_THREAD

        std::string data;

        if ( !loadRestartData( m_restartRootPath / path, data ))
        {
            return false;
        }

        std::istringstream is( data, std::ios::binary );
        cereal::PortableBinaryInputArchive iarchive( is );
        iarchive( value );
        return true;
    }

private:

    void saveRestartData( std::string outputFile, const std::string data ) const
    {
        try
        {
            {
                std::ofstream fStream( outputFile + ".tmp", std::ios::binary );
                fStream << data;
            }
            _ASSERT( fs::exists( outputFile + ".tmp" ))
            std::error_code err;
            fs::remove( outputFile, err );
            fs::rename( outputFile + ".tmp", outputFile, err );
        }
        catch (const std::exception& ex)
        {
            _LOG_ERR( "exception during saveRestartData: " << ex.what());
        }
    }

    bool loadRestartData( std::string outputFile, std::string& data ) const
    {
        std::error_code err;

        if ( fs::exists( outputFile, err ))
        {
            std::ifstream ifStream( outputFile, std::ios::binary );
            if ( ifStream.is_open())
            {
                std::ostringstream os;
                os << ifStream.rdbuf();
                data = os.str();
                return true;
            }
        }

        if ( fs::exists( outputFile + ".tmp", err ))
        {
            std::ifstream ifStream( outputFile + ".tmp", std::ios::binary );
            if ( ifStream.is_open())
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

class DefaultModifyOpinionController: public ModifyOpinionController
{

private:

    Key                                         m_driveKey;

    const RestartValueSerializer&               m_serializer;

    // It is needed for right calculation of my 'modify' opinion
    std::optional<std::array<uint8_t,32>>       m_opinionTrafficIdentifier; // (***)
    uint64_t                                    m_expectedCumulativeDownload;
    uint64_t                                    m_accountedCumulativeDownload; // (***)
    std::map<std::array<uint8_t,32>, uint64_t>  m_cumulativeUploads; // (***)
    std::map<std::array<uint8_t,32>, uint64_t>  m_lastAccountedUploads; // (***)

    // opinions from other replicators
    // key of the outer map is modification id
    // key of the inner map is a replicator key, one replicator one opinion
    std::map<Hash256, std::map<std::array<uint8_t,32>,ApprovalTransactionInfo>>    m_otherOpinions; // (***)

    std::thread::id     m_dbgThreadId;
    std::string         m_dbgOurPeerName;

public:

    DefaultModifyOpinionController(
            const Key& driveKey,
            const RestartValueSerializer& serializer,
            uint64_t expectedCumulativeDownload,
            const std::string& dbgOurPeerName )
            : m_driveKey( driveKey )
            , m_serializer( serializer )
            , m_expectedCumulativeDownload( expectedCumulativeDownload )
            , m_accountedCumulativeDownload( 0 )
            , m_dbgThreadId( std::this_thread::get_id())
            , m_dbgOurPeerName( dbgOurPeerName )
    {}

    void initialize()
    {
        DBG_BG_THREAD
    }

    void downgradeCumulativeUploads( const std::function<void()>& callback ) override
    {
        DBG_MAIN_THREAD
    }

    void increaseExpectedCumulativeDownload( uint64_t add ) override
    {
        DBG_MAIN_THREAD

        m_expectedCumulativeDownload += add;
    }

    void createMyOpinion( const Hash256& sandboxRootHash, const uint64_t& sandboxDriveSize,
                          const uint64_t& sandboxFsTreeSize, const uint64_t& sandboxMetafilesSize,
                          const std::function<void( const ApprovalTransactionInfo& )>& callback ) override
    {
        DBG_MAIN_THREAD
    }

    std::optional<Hash256>& getOpinionTrafficIdentifier() override
    {
        return <#initializer#>;
    }

    std::map<std::array<uint8_t, 32>, ApprovalTransactionInfo>& getOtherOpinions( const Hash256& modification ) override
    {
        return m_otherOpinions[modification];
    }

private:

    // m_myOpinion
    void saveEmptyOpinion()
    {
        std::optional<ApprovalTransactionInfo> opinion;
        m_serializer.saveRestartValue( opinion, "myOpinion" );
    }
    void saveMyOpinion()
    {
        m_serializer.saveRestartValue( m_myOpinion, "myOpinion" );
    }
    void loadMyOpinion()
    {
        m_serializer.loadRestartValue( m_myOpinion, "myOpinion" );
    }

    // m_opinionTrafficIdentifier
    void saveOpinionTrafficIdentifier()
    {
        m_serializer.saveRestartValue( m_opinionTrafficIdentifier, "opinionTrafficIdentifier" );
    }
    void loadOpinionTrafficIdentifier()
    {
        m_serializer.loadRestartValue( m_opinionTrafficIdentifier, "opinionTrafficIdentifier" );
    }

    // m_accountedCumulativeDownload
    void saveAccountedCumulativeDownload()
    {
        m_serializer.saveRestartValue( m_accountedCumulativeDownload, "accountedCumulativeDownload" );
    }
    void loadAccountedCumulativeDownload()
    {
        if ( !m_serializer.loadRestartValue( m_accountedCumulativeDownload, "accountedCumulativeDownload" ) )
        {
            m_accountedCumulativeDownload = 0;
        }
    }

    // m_cumulativeUploads
    void saveCumulativeUploads()
    {
        m_serializer.saveRestartValue( m_cumulativeUploads, "cumulativeUploads" );
    }
    void loadCumulativeUploads()
    {
        m_serializer.loadRestartValue( m_cumulativeUploads, "cumulativeUploads" );
    }

    // m_lastAccountedUploads
    void saveLastAccountedUploads()
    {
        m_serializer.saveRestartValue( m_lastAccountedUploads, "lastAccountedUploads" );
    }
    void loadLastAccountedUploads()
    {
        m_serializer.loadRestartValue(  m_lastAccountedUploads, "lastAccountedUploads" );
    }
};

//
// DefaultDrive - it manages all user files at replicator side
//
class DefaultFlatDrive: public FlatDrive,
                        public TaskController,
                        protected FlatDrivePaths {

    using uint128_t = boost::multiprecision::uint128_t;

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

    // Serializer
    RestartValueSerializer          m_serializer;

    // Opinion Controller
    DefaultModifyOpinionController  m_opinionController;


    // Modify status
    bool                        m_modifyUserDataReceived       = false;
    bool                        m_sandboxCalculated            = false;
    bool                        m_isSynchronizing              = false;
    bool                        m_modifyApproveTransactionSent = false;
    std::optional<Hash256>      m_receivedModifyApproveTx;

    // Verify status
    bool                                    m_myVerifyCodesCalculated = false;
    bool                                    m_verifyApproveTxSent   = false;
    std::optional<boost::posix_time::ptime> m_verificationStartedAt;
    std::vector<mobj<VerificationCodeInfo>> m_unknownVerificationCodeQueue;
    std::map<std::array<uint8_t,32>,mobj<VerificationCodeInfo>> m_receivedVerificationCodes;
    
    mobj<VerifyApprovalTxInfo>      m_myVerifyAprovalTxInfo;
    std::vector<mobj<VerifyApprovalTxInfo>> m_unknownOpinions;

    // Verification will be canceled, after any modify tx has been published
    std::atomic_bool                m_verificationMustBeInterrupted = false;

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
    std::deque<ModificationRequest>     m_deferredModifyRequests;
    mobj<VerificationRequest>           m_deferredVerificationRequest;

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

    std::vector<uint64_t>               m_verificationCodes;

    // FsTree
    FsTree        m_fsTree;
    lt_handle     m_fsTreeLtHandle; // used for removing FsTree torrent from session

    // Root hashes
    InfoHash      m_sandboxRootHash;

    //
    // 'modify' opinion
    //
    std::optional<ApprovalTransactionInfo>              m_myOpinion; // (***)


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

    //
    std::shared_ptr<BaseDriveTask> m_task;

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
          m_serializer(m_restartRootPath, replicator.dbgReplicatorName()),
          m_opinionController(m_drivePubKey, m_serializer, expectedCumulativeDownload, replicator.dbgReplicatorName() ),
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
        DBG_MAIN_THREAD

        m_shareMyOpinionTimer.reset();
        m_verifyCodeTimer.reset();
        m_modifyOpinionTimer.reset();
        m_verifyOpinionTimer.reset();

        m_backgroundExecutor.stop();
        interruptVerification();
        
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

    ReplicatorList getUploaders()
    {
        auto replicators = getReplicators();
        replicators.push_back( getClient() );
        return replicators;
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

        if ( !fs::exists( m_restartRootPath, err ) )
        {
            fs::create_directories( m_restartRootPath, err );
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

    bool downgradeCumulativeUploads()
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

        return !trafficIdentifierHasValue;
    }

    void maybeRunTask()
    {
        if ( !m_task )
        {
            _ASSERT( !m_deferredTasks.empty() )
        }
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

        if( m_deferredVerificationRequest )
        {
            if (m_deferredVerificationRequest->m_actualRootHash == m_rootHash )
            {
                m_verificationRequest = std::move(m_deferredVerificationRequest );
                runVerifyDriveTaskOnSeparateThread();

                // Verification will be performed on separate thread.
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
        
        if ( !m_deferredModifyRequests.empty() )
        {
            auto request = m_deferredModifyRequests.front();
            m_deferredModifyRequests.pop_front();
            
            startModifyDriveTask( request );
            return;
        }

    }
    

    
    uint64_t calcHash64( uint64_t initValue, uint8_t* begin, uint8_t* end )
    {
        _ASSERT( begin < end )
        
        uint64_t hash = initValue;
        uint8_t* ptr = begin;

        // At first, we process 8-bytes chunks
        for( ; ptr+8 <= end; ptr+=8 )
        {
            hash ^= *reinterpret_cast<uint64_t*>(ptr);

            // Circular Shift
            if ( hash&0x1 )
            {
                hash = (hash >> 1) | 0x8000000000000000;
            }
            else
            {
                hash = (hash >> 1);
            }
        }

        // At the end, we process tail
        uint64_t lastValue = 0;
        for( ; ptr < end; ptr++ )
        {
            lastValue = lastValue << 8;
            lastValue |= *ptr;
        }

        hash ^= lastValue;
        if ( hash&0x1 )
        {
            hash = (hash >> 1) | 0x8000000000000000;
        }
        else
        {
            hash = (hash >> 1);
        }

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
        m_verificationMustBeInterrupted = false;
        m_myVerifyAprovalTxInfo.reset();
        m_receivedVerificationCodes.clear();

        _ASSERT( !m_verificationStartedAt )
        m_verificationStartedAt = boost::posix_time::microsec_clock::universal_time();

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
        // ???
        std::remove_if( m_unknownVerificationCodeQueue.begin(), m_unknownVerificationCodeQueue.end(), []( const auto& it ) {
            return !it;
        });

        //
        // Run verification task on separate thread
        //
        m_verifyThread = std::thread( [this,verificationRequest=*m_verificationRequest] () mutable
        {
            // commented because of races
//            DBG_VERIFY_THREAD
            
            m_verificationCodes.clear();
            m_verificationCodes = std::vector<uint64_t>( verificationRequest.m_replicators.size(), 0 );
            
            for( uint32_t i=0; i<m_verificationCodes.size(); i++ )
            {
                uint64_t initHash = calcHash64( 0, verificationRequest.m_tx.begin(), verificationRequest.m_tx.end() );
                initHash = calcHash64( initHash, verificationRequest.m_replicators[i].begin(), verificationRequest.m_replicators[i].end() );
                m_verificationCodes[i] = initHash;
            }
            
            calculateVerifyCodes( m_fsTree );
            
            executeOnSessionThread( [this, verificationTx=verificationRequest.m_tx] {
                verificationCodesCompleted(verificationTx);
            });
        });
    }

    void calculateVerifyCodes( const Folder& folder )
    {
//        DBG_VERIFY_THREAD
        
        for( const auto& child : folder.m_childs )
        {
            if ( m_verificationMustBeInterrupted )
            {
                break;
            }

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

                while( !m_verificationMustBeInterrupted )
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

    void verificationCodesCompleted(const Hash256& verificationTx)
    {
        DBG_MAIN_THREAD

        if ( !m_verificationRequest || m_verificationRequest->m_tx != verificationTx)
        {
            // 'Verify Approval Tx' already published or canceled (we are late)
            return;
        }

        m_myVerifyCodesCalculated = true;

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

    //
    // CANCEL
    //

    void cancelModifyDrive( const Hash256& transactionHash ) override
    {
        DBG_MAIN_THREAD

        if ( m_driveIsInitializing )
        {
            auto it = std::find_if(m_deferredModifyRequests.begin(), m_deferredModifyRequests.end(), [&transactionHash](const auto& item)
                                { return item.m_transactionHash == transactionHash; });
            
            if (it != m_deferredModifyRequests.end() )
            {
                m_deferredModifyRequests.erase(it );
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
            auto it = std::find_if(m_deferredModifyRequests.begin(), m_deferredModifyRequests.end(), [&transactionHash](const auto& item)
                                { return item.m_transactionHash == transactionHash; });
            
            if (it == m_deferredModifyRequests.end() )
            {
                _LOG_ERR( "cancelModifyDrive(): invalid transactionHash: " << transactionHash );
                return;
            }
            
            m_deferredModifyRequests.erase(it );
        }
    }

    void runCancelModifyDriveTask( std::optional<Hash256>&& transactionHash )
    {
        DBG_MAIN_THREAD

        _ASSERT( transactionHash );

        m_modificationCanceledTx = transactionHash;


        bool updatedCumulativeUploads = downgradeCumulativeUploads();

        for( auto& it : m_torrentHandleMap )
            it.second.m_isUsed = false;

        markUsedFiles( m_fsTree );

        m_backgroundExecutor.run([=, this]
        {
            saveAccountedCumulativeDownload();
            if ( updatedCumulativeUploads )
            {
                saveCumulativeUploads();
            }
            else
            {
                saveOpinionTrafficIdentifier();
            }


        });
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

        if ( !m_verificationRequest )
        {
            _LOG_ERR( "verifyApprovalPublished: internal error: m_verificationRequest == null" )
            return;
        }

        if ( m_verificationRequest->m_tx != info.m_tx )
        {
            _LOG_ERR( "verifyApprovalPublished: internal error: invalid tx" )
            return;
        }

        m_verifyCodeTimer.reset();
        m_verifyOpinionTimer.reset();
        m_verificationRequest.reset();
        m_verificationStartedAt.reset();

        interruptVerification();
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
        
        if ( !m_verificationRequest || m_verificationRequest->m_tx != info->m_tx )
        {
            // opinion is old or new
            _LOG( "processVerificationOpinion: received unknown verification opinion: " << Hash256(info->m_tx) )
            m_unknownOpinions.emplace_back( std::move(info) );
            return;
        }

        if ( m_verificationRequest->m_shardId != info->m_shardId )
        {
            _LOG_WARN( "processVerificationOpinion: received opinion with different m_shardId: " << info->m_shardId << " vs " << m_verificationRequest->m_shardId )
            return;
        }

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

        if ( info->m_opinions[0].m_opinions.size() != m_verificationRequest->m_replicators.size() )
        {
            _LOG_WARN( "processVerificationOpinion: incorrect number of replicators in opinion: " << info->m_opinions[0].m_opinions.size() << " vs " << m_verificationRequest->m_replicators.size() )
            return;
        }

        if ( !m_myVerifyAprovalTxInfo )
        {
            m_myVerifyAprovalTxInfo = std::move(info);
            return;
        }

        _ASSERT( m_myVerifyAprovalTxInfo->m_tx == info->m_tx )

        // At any case opinions with the same replicator key must be removed
        //
        auto& opinions = m_myVerifyAprovalTxInfo->m_opinions;
        std::remove_if( opinions.begin(), opinions.end(), [&info] (const auto& it) {
            return it.m_publicKey == info->m_opinions[0].m_publicKey;
        });

        m_myVerifyAprovalTxInfo->m_opinions.emplace_back( info->m_opinions[0] );

        if ( m_myVerifyAprovalTxInfo->m_opinions.size() > (m_verificationRequest->m_replicators.size() * 2 ) / 3 )
        {
            // start timer if it is not started
            if ( !m_verifyOpinionTimer )
            {
                if ( auto session = m_session.lock(); session )
                {
                    m_verifyOpinionTimer = session->startTimer( m_replicator.getVerifyApprovalTransactionTimerDelay(),
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

        _ASSERT( m_verificationRequest )
        _ASSERT( m_myVerifyCodesCalculated )

        auto replicatorNumber = m_verificationRequest->m_replicators.size();

        // check code number
        if ( m_receivedVerificationCodes.size() == replicatorNumber-1 )
        {
            m_verifyCodeTimer.reset();
            verifyCodeTimerExpired();
        }
        else if ( !m_verifyApproveTxSent )
        {
            // start timer if it is not started
            if ( !m_verifyCodeTimer )
            {
                _ASSERT( m_verificationStartedAt )

                auto secondsSinceVerificationStart =
                        (boost::posix_time::microsec_clock::universal_time() - *m_verificationStartedAt).total_seconds();
                int codesDelay;
                if ( m_verificationRequest->m_durationMs > secondsSinceVerificationStart + m_replicator.getVerifyCodeTimerDelay() )
                {
                    codesDelay = int(m_verificationRequest->m_durationMs - secondsSinceVerificationStart + m_replicator.getVerifyCodeTimerDelay());
                }
                else
                {
                    codesDelay = 0;
                }

                if ( auto session = m_session.lock(); session )
                {
                    m_verifyCodeTimer = session->startTimer( codesDelay,
                                        [this]() { verifyCodeTimerExpired(); } );
                }
            }
        }
    }

    void verifyCodeTimerExpired()
    {
        DBG_MAIN_THREAD

        if ( !m_verificationRequest )
        {
            return;
        }

        _ASSERT( m_myVerifyCodesCalculated )
        _ASSERT( !m_verifyApproveTxSent )

        // Prepare 'Verify Approval Tx Info'
        VerifyApprovalTxInfo info {
                                        m_verificationRequest->m_tx.array(),
                                        m_drivePubKey.array(),
                                        m_verificationRequest->m_shardId,
            {} };

        VerifyOpinion myOpinion = {m_replicator.replicatorKey().array(), {}, {} };

        auto& keyList = m_verificationRequest->m_replicators;
        myOpinion.m_opinions.resize( keyList.size() );

        for( size_t i=0; i<keyList.size(); i++ )
        {
            auto& key = keyList[i].array();

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
                    myOpinion.m_opinions[i] = 0;
                }
            }
        }

        myOpinion.Sign( m_replicator.keyPair(), info.m_tx, info.m_driveKey, info.m_shardId );

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
        
        if ( tx->array() != m_verificationRequest->m_tx.array() )
        {
            _LOG_ERR( "cancelVerification: internal error: bad tx:" << int(tx->array()[0]) )
            return;
        }

        m_verifyCodeTimer.reset();
        m_verifyOpinionTimer.reset();
        m_verificationRequest.reset();
        m_verificationStartedAt.reset();

        interruptVerification();
    }

    void interruptVerification()
    {
        DBG_MAIN_THREAD

        m_verificationMustBeInterrupted = false;

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

        m_deferredVerificationRequest = std::move(request);

        if ( m_driveIsInitializing )
        {
            return;
        }

        if ( m_verificationRequest )
        {
            _LOG_ERR("startVerification: internal error: m_verificationRequest != null")
        }
        
        if (m_deferredVerificationRequest->m_actualRootHash == m_rootHash )
        {
            if ( m_isSynchronizing )
            {
                // Outdated request root hash (not properly extracted rootHash from blockchain)
                _LOG_ERR( "startVerification: m_isSynchronizing must be false" )
            }
            _ASSERT( !m_isSynchronizing )
            m_verificationRequest = std::move(m_deferredVerificationRequest );
            runVerifyDriveTaskOnSeparateThread();
        }
    }

    void runDriveClosingTask( std::optional<Hash256>&& transactionHash )
    {

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
            m_deferredModifyRequests.emplace_back(std::move(modifyRequest) );
            return;
        }
        startModifyDriveTask( modifyRequest );
    }
    
    void startModifyDriveTask( const ModifyRequest& modifyRequest )
    {

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
        _ASSERT(modificationUploads.contains(getClient().array()))

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
                if ( key != getClient().array() )
                {
                    auto longUploadBytes = (uploadBytes * longTargetSum) / sumBefore;
                    uploadBytes = longUploadBytes.convert_to<uint64_t>();
                    sumAfter += uploadBytes;
                }
            }
            modificationUploads[getClient().array()] = targetSum - sumAfter;
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
                m_lastAccountedUploads[replicatorIt.array()] = it->second.m_receivedSize;
            } else
            {
                m_lastAccountedUploads[replicatorIt.array()] = 0;
            }
        }

        if (auto it = modifyTrafficMap.find(getClient().array());
                it != modifyTrafficMap.end())
        {
            m_lastAccountedUploads[getClient().array()] = it->second.m_receivedSize;
        } else
        {
            m_lastAccountedUploads[getClient().array()] = 0;
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


    //////////////// Opinion Task Controller
    
    void createMyOpinion( const Hash256& sandboxRootHash,
                          const uint64_t& sandboxDriveSize,
                          const uint64_t& sandboxFsTreeSize,
                          const uint64_t& sandboxMetafilesSize,
                          const std::function<void( const ApprovalTransactionInfo& )>& callback ) override
    {
        DBG_MAIN_THREAD
//        _ASSERT( m_opinionTrafficIdentifier )
//        _ASSERT( !m_myOpinion )
//        _ASSERT( m_modifyRequest.has_value() != m_catchingUpRequest.has_value() )
//        _ASSERT( !m_modificationCanceledTx )

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
            auto it = m_cumulativeUploads.find( getClient().array() );
            opinion.m_clientUploadBytes = it->second;
        }

        opinion.Sign( m_replicator.keyPair(),
                      drivePublicKey(),
                      modificationId,
                      sandboxRootHash,
                      sandboxFsTreeSize,
                      sandboxMetafilesSize,
                      sandboxDriveSize);

        m_myOpinion = std::optional<ApprovalTransactionInfo> {{ m_drivePubKey.array(),
                                                                modificationId.array(),
                                                                m_sandboxRootHash.array(),
                                                                sandboxFsTreeSize,
                                                                sandboxMetafilesSize,
                                                                sandboxDriveSize,
                                                                { std::move(opinion) }}};

        m_backgroundExecutor.run([=, this] {
            saveMyOpinion();
            saveOpinionTrafficIdentifier();
            saveCumulativeUploads();
            saveLastAccountedUploads();
            saveAccountedCumulativeDownload();
            executeOnSessionThread([=] {
                callback(m_myOpinion);
            });
        });
    }


    ////////////////

    void myRootHashIsCalculated()
    {
        DBG_MAIN_THREAD

        _ASSERT( m_modifyRequest.has_value() != m_catchingUpRequest.has_value() )
        
        if ( m_newCatchingUpRequest || m_modificationMustBeCanceledTx || m_removeDriveTx )
        {
            runNextTask();
            return;
        }

        // Notify
        if ( m_dbgEventHandler )
            m_dbgEventHandler->rootHashIsCalculated( m_replicator, m_drivePubKey, m_modifyRequest->m_transactionHash, m_sandboxRootHash );
        
        // Calculate my opinion
        createMyOpinion();
    }

    void myOpinionIsCreated()
    {

    }

    void onOpinionReceived( const ApprovalTransactionInfo& anOpinion ) override
    {
        DBG_MAIN_THREAD
        
        // Preliminary opinion verification takes place at extension

        if ( !m_task || !m_task->processedOpinion(anOpinion))
        {
            m_otherOpinions[anOpinion.m_modifyTransactionHash][anOpinion.m_opinions[0].m_replicatorKey] = anOpinion;
        }
    }

    void onApprovalTransactionHasBeenPublished( const PublishedModificationApprovalTransactionInfo& transaction ) override
    {
        DBG_MAIN_THREAD

        if ( m_driveIsInitializing )
        {
            m_publishedTxDuringInitialization = transaction;
            return;
        }

        if ( !m_task || m_task->shouldCatchUp( transaction ) != NotificationProcessResult::SUCCESS )
        {
            m_newCatchingUpRequest = { transaction.m_rootHash, transaction.m_modifyTransactionHash };

            if ( !m_task )
            {
                runNextTask();
            }
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

    void onSingleApprovalTransactionHasBeenPublished( const PublishedModificationSingleApprovalTransactionInfo& transaction ) override
    {
        _LOG( "onSingleApprovalTransactionHasBeenPublished()" );
    }

    void startCatchingUpTask( std::optional<CatchingUpRequest>&& actualCatchingRequest )
    {

    }
    
    void completeCatchingUp()
    {

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

    void dbgPrintDriveStatus() override
    {
        LOG("Drive Status:")
        m_fsTree.dbgPrint();
        if ( auto session = m_session.lock(); session )
        {
            session->printActiveTorrents();
        }
    }

    virtual uint64_t& getExpectedCumulativeDownload() override
    {
        return m_expectedCumulativeDownload;
    }

    virtual uint64_t& getAccountedCumulativeDownload() override
    {
        return m_accountedCumulativeDownload;
    }
    
    
    //-----------------------------------------------------------------------------
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

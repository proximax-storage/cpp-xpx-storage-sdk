/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "drive/FlatDrive.h"
#include "drive/Replicator.h"
#include "drive/Session.h"
#include "drive/BackgroundExecutor.h"
#include "DriveTask.h"
#include "TaskContext.h"
#include "drive/Utils.h"
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

class DefaultModifyOpinionController: public ModifyOpinionController
{

    using uint128_t = boost::multiprecision::uint128_t;

private:

    Key                                         m_driveKey;

    Key                                         m_clientKey;

    Replicator&                                 m_replicator;
    const RestartValueSerializer&               m_serializer;
    ThreadManager&                              m_threadManager;

    // It is needed for right calculation of my 'modify' opinion
    std::optional<std::array<uint8_t, 32>>      m_opinionTrafficIdentifier; // (***)
    uint64_t                                    m_approvedExpectedCumulativeDownload;
//    uint64_t                                    m_accountedCumulativeDownload; // (***)
    std::map<std::array<uint8_t,32>, uint64_t>  m_approvedCumulativeUploads; // (***)
    std::map<std::array<uint8_t,32>, uint64_t>  m_notApprovedAccountedUploads; // (***)

    std::thread::id     m_dbgThreadId;
    std::string         m_dbgOurPeerName;

public:

    DefaultModifyOpinionController(
            const Key& driveKey,
            const Key& client,
            Replicator& replicator,
            const RestartValueSerializer& serializer,
            ThreadManager& threadManager,
            uint64_t expectedCumulativeDownload,
            const std::string& dbgOurPeerName )
            : m_driveKey( driveKey ), m_clientKey( client )
            , m_replicator( replicator )
            , m_serializer( serializer )
            , m_threadManager( threadManager )
            , m_approvedExpectedCumulativeDownload( expectedCumulativeDownload )
            , m_dbgThreadId( std::this_thread::get_id())
            , m_dbgOurPeerName( dbgOurPeerName )
    {}

    void initialize() override
    {
        DBG_BG_THREAD

        loadApprovedCumulativeUploads();
        loadNotApprovedCumulativeUploads();
    }

    void increaseApprovedExpectedCumulativeDownload( uint64_t add ) override
    {
        DBG_MAIN_THREAD

        _LOG( "increased " << add);

        m_approvedExpectedCumulativeDownload += add;
    }

    std::optional<Hash256> getOpinionTrafficIdentifier() override
    {
        DBG_MAIN_THREAD

        return m_opinionTrafficIdentifier;
    }

    void setOpinionTrafficIdentifier(const Hash256& identifier) override
    {
        DBG_MAIN_THREAD

        m_opinionTrafficIdentifier = identifier.array();
    }

    void updateCumulativeUploads( const ReplicatorList& replicators,
                                  uint64_t addCumulativeDownload,
                                  const std::function<void()>& callback ) override
    {
        DBG_MAIN_THREAD

        const auto &modifyTrafficMap = m_replicator.getMyDownloadOpinion(*m_opinionTrafficIdentifier)
                .m_modifyTrafficMap;

        std::map<std::array<uint8_t,32>, uint64_t> currentUploads;
        for (const auto &replicatorIt : replicators)
        {
            // get data size received from 'replicatorIt.m_publicKey'
            if (auto it = modifyTrafficMap.find(replicatorIt.array());
            it != modifyTrafficMap.end())
            {
                currentUploads[replicatorIt.array()] = it->second.m_receivedSize;
            } else
            {
                currentUploads[replicatorIt.array()] = 0;
            }
        }

        if (auto it = modifyTrafficMap.find(m_clientKey.array());
        it != modifyTrafficMap.end())
        {
            currentUploads[m_clientKey.array()] = it->second.m_receivedSize;
        } else
        {
            currentUploads[m_clientKey.array()] = 0;
        }

        auto accountedCumulativeDownload = std::accumulate( m_notApprovedAccountedUploads.begin(),
                                                            m_notApprovedAccountedUploads.end(), 0,
                                                            []( const auto& sum, const auto& item )
                                                            {
                                                                return sum + item.second;
                                                            } );
        auto expectedCumulativeDownload = m_approvedExpectedCumulativeDownload + addCumulativeDownload;

        _ASSERT( expectedCumulativeDownload > 0 )

        uint64_t targetSize = expectedCumulativeDownload - accountedCumulativeDownload;
        normalizeUploads(currentUploads, targetSize);
        m_replicator.removeModifyDriveInfo( *m_opinionTrafficIdentifier );
        m_opinionTrafficIdentifier.reset();

        for (const auto&[uploaderKey, bytes]: currentUploads)
        {
            if (m_notApprovedAccountedUploads.find(uploaderKey) == m_notApprovedAccountedUploads.end())
            {
                m_notApprovedAccountedUploads[uploaderKey] = 0;
            }
            m_notApprovedAccountedUploads[uploaderKey] += bytes;
        }

        m_threadManager.executeOnBackgroundThread([=, this] {
            saveNotApprovedCumulativeUploads();
            m_threadManager.executeOnSessionThread([=] {
                callback();
            });
        });
    }

    void fillOpinion( std::vector<KeyAndBytes>& replicatorsUploads,
                      uint64_t& clientUploads ) override
    {
        DBG_MAIN_THREAD

        _ASSERT (replicatorsUploads.empty())

        for ( const auto&[key, bytes] : m_notApprovedAccountedUploads )
        {
            if ( key != m_clientKey.array())
            {
                replicatorsUploads.push_back( {key, bytes} );
            }
            else {
                clientUploads = bytes;
            }
        }
    }

    void approveCumulativeUploads( const std::function<void()>& callback ) override
    {
        DBG_MAIN_THREAD

        m_approvedCumulativeUploads = m_notApprovedAccountedUploads;
        m_approvedExpectedCumulativeDownload = std::accumulate( m_approvedCumulativeUploads.begin(),
                                                                m_approvedCumulativeUploads.end(), 0,
                                                                []( const auto& sum, const auto& item )
                                                                {
                                                                    return sum + item.second;
                                                                } );

        m_threadManager.executeOnBackgroundThread( [=, this]
        {
            saveApprovedCumulativeUploads();
            m_threadManager.executeOnSessionThread( [=]
            {
                callback();
            } );
        } );
    }

    void disapproveCumulativeUploads( const std::function<void()>& callback ) override
    {

        DBG_MAIN_THREAD

        // We have already taken into account information
        // about uploads of the modification to be canceled;
        auto trafficIdentifierHasValue = m_opinionTrafficIdentifier.has_value();
        if ( trafficIdentifierHasValue )
        {
            m_replicator.removeModifyDriveInfo( *m_opinionTrafficIdentifier );
            m_opinionTrafficIdentifier.reset();
        }

        m_notApprovedAccountedUploads = m_approvedCumulativeUploads;

        m_threadManager.executeOnBackgroundThread( [=, this]
                                                   {
                                                       saveNotApprovedCumulativeUploads();
                                                       m_threadManager.executeOnSessionThread( [=]
                                                                                               {
                                                                                                   callback();
                                                                                               } );
                                                   } );
    }

private:

    void normalizeUploads(std::map<std::array<uint8_t,32>, uint64_t>& modificationUploads, uint64_t targetSum)
    {
        DBG_MAIN_THREAD

        _ASSERT(modificationUploads.contains(m_clientKey.array()))

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
                if ( key != m_clientKey.array() )
                {
                    auto longUploadBytes = (uploadBytes * longTargetSum) / sumBefore;
                    uploadBytes = longUploadBytes.convert_to<uint64_t>();
                    sumAfter += uploadBytes;
                }
            }
            modificationUploads[m_clientKey.array()] = targetSum - sumAfter;
        }
    }

    void saveApprovedCumulativeUploads()
    {
        DBG_BG_THREAD

        m_serializer.saveRestartValue( m_approvedCumulativeUploads, "cumulativeUploads" );
    }
    void loadApprovedCumulativeUploads()
    {
        DBG_BG_THREAD

        m_serializer.loadRestartValue( m_notApprovedAccountedUploads, "cumulativeUploads" );
    }

    void saveNotApprovedCumulativeUploads()
    {
        DBG_BG_THREAD

        m_serializer.saveRestartValue( m_notApprovedAccountedUploads, "lastAccountedUploads" );
    }
    void loadNotApprovedCumulativeUploads()
    {
        DBG_BG_THREAD

        m_serializer.loadRestartValue(  m_notApprovedAccountedUploads, "lastAccountedUploads" );
    }
};

//
// DefaultDrive - it manages all user files at replicator side
//
class DefaultFlatDrive: public FlatDrive, public TaskContext
{
    // List of all replicators that support this drive
    // (It does not contain our replicator key!)
    ReplicatorList m_allReplicators;

    const size_t m_maxSize;

    BackgroundExecutor      m_backgroundExecutor;

    // Opinion Controller
    DefaultModifyOpinionController  m_opinionController;

    // opinions from other replicators
    // key of the outer map is modification id
    // key of the inner map is a replicator key, one replicator one opinion
    std::map<Hash256, std::map<std::array<uint8_t,32>,ApprovalTransactionInfo>>    m_unknownModificationOpinions; // (***)
    std::map<Hash256, std::vector<VerifyApprovalTxInfo>>                           m_unknownVerificationOpinions;

    //
    // Request queue
    //

    mobj<DriveClosureRequest> m_closeDriveRequest = {};
    mobj<ModificationCancelRequest> m_modificationCancelRequest;
    mobj<CatchingUpRequest> m_catchingUpRequest;
    std::deque<mobj<ModificationRequest>> m_deferredModificationRequests;
    mobj<VerificationRequest> m_deferredVerificationRequest;

    std::map<Hash256, std::vector<VerificationCodeInfo>>  m_unknownVerificationCodeQueue;

    //
    std::unique_ptr<BaseDriveTask> m_task;
    std::shared_ptr<BaseDriveTask> m_verificationTask;

    ReplicatorList m_modifyDonatorShard;
    ReplicatorList m_modifyRecipientShard;

public:

    DefaultFlatDrive(
            std::shared_ptr<Session> session,
            const std::string& replicatorRootFolder,
            const std::string& replicatorSandboxRootFolder,
            const Key& drivePubKey,
            const Key& clientPubKey,
            size_t maxSize,
            size_t expectedCumulativeDownload,
            ReplicatorEventHandler& eventHandler,
            Replicator& replicator,
            const ReplicatorList& initialReplicatorList,
            DbgReplicatorEventHandler* dbgEventHandler )
            : TaskContext(
                    drivePubKey,
                    clientPubKey,
                    session,
                    eventHandler,
                    replicator,
                    dbgEventHandler,
                    replicatorRootFolder,
                    replicatorSandboxRootFolder,
                    replicator.dbgReplicatorName())
            , m_allReplicators(initialReplicatorList)
            , m_maxSize(maxSize)
            , m_opinionController(m_driveKey, m_client, m_replicator, m_serializer, *this, expectedCumulativeDownload, replicator.dbgReplicatorName() )

    {
        runDriveInitializationTask();
    }

    virtual~DefaultFlatDrive() {
    }

    const Key& drivePublicKey() const override { return m_driveKey; }

    void terminate() override
    {
        DBG_MAIN_THREAD

        if ( m_task )
        {
            m_task->terminate();
        }

        if ( m_verificationTask )
        {
            m_verificationTask->terminate();
            m_verificationTask.reset();
        }

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
    
    const ReplicatorList& getAllReplicators() const override {
        return m_allReplicators;
    }

    const Key& getClient() const override
    {
        return m_client;
    }

    void replicatorAdded( mobj<Key>&& replicatorKey ) override
    {
        if ( *replicatorKey != m_replicator.replicatorKey() )
        {
            if ( std::find( m_allReplicators.begin(), m_allReplicators.end(), *replicatorKey ) == m_allReplicators.end() )
            {
                m_allReplicators.push_back( *replicatorKey );
            }
        }
    }

    void replicatorRemoved( mobj<Key>&& replicatorKey ) override
    {
        std::remove_if( m_allReplicators.begin(), m_allReplicators.end(), [&] (const Key& it)
        {
            return it == *replicatorKey;
        });
    }


    void executeOnSessionThread( const std::function<void()>& task ) override
    {
        if ( auto session = m_session.lock(); session )
        {
            boost::asio::post(session->lt_session().get_context(), task);
        }
    }

    void executeOnBackgroundThread( const std::function<void()>& task ) override
    {
        m_backgroundExecutor.run( [=]
                                  {
                                      task();
                                  } );
    }
    
    void runNextTask() override
    {
        DBG_MAIN_THREAD
        
        m_task.reset();

        if ( m_closeDriveRequest )
        {
            runCloseDriveTask();
            return;
        }

        if( m_deferredVerificationRequest )
        {
            if (m_deferredVerificationRequest->m_actualRootHash == m_rootHash )
            {
                runVerificationTask();

                // Verification will be performed on separate thread.
                // And we should try to run other task.
                // So, do not return;
            }
        }

        if ( m_modificationCancelRequest )
        {
            runCancelModificationTask();
            return;
        }
        
        if ( m_catchingUpRequest )
        {
            runCatchingUpTask();
            return;
        }
        
        if ( !m_deferredModificationRequests.empty() )
        {
            runModificationTask();
            return;
        }
    }

    void runDriveInitializationTask()
    {
        DBG_MAIN_THREAD

        _ASSERT( !m_task )

        m_task = createDriveInitializationTask( *this, m_opinionController );

        _ASSERT( m_task->getTaskType() == DriveTaskType::DRIVE_INITIALIZATION )

        m_task->run();
    }

    void runModificationTask()
    {
        DBG_MAIN_THREAD

        _ASSERT( !m_task )

        auto request = m_deferredModificationRequests.front();
        m_deferredModificationRequests.pop_front();

        auto opinions = std::move(m_unknownModificationOpinions[request->m_transactionHash]);
        m_unknownModificationOpinions.erase(request->m_transactionHash);

        m_task = createModificationTask( std::move(request), std::move(opinions), *this, m_opinionController );

        _ASSERT( m_task->getTaskType() == DriveTaskType::MODIFICATION_REQUEST )

        m_task->run();
    }

    void runCatchingUpTask()
    {
        DBG_MAIN_THREAD

        _ASSERT( !m_task )

        // clear modification queue - we will not execute these modifications
        auto it = std::find_if( m_deferredModificationRequests.begin(), m_deferredModificationRequests.end(), [&]( const auto& item )
            {
                return item->m_transactionHash ==
                       m_catchingUpRequest->m_modifyTransactionHash;
            } );
        if (it != m_deferredModificationRequests.end() )
        {
            const auto& opinionTrafficIdentifier = m_opinionController.getOpinionTrafficIdentifier();
            while (!m_deferredModificationRequests.empty() and it != m_deferredModificationRequests.begin() )
            {
                if ( !opinionTrafficIdentifier
                || *opinionTrafficIdentifier != m_deferredModificationRequests.front()->m_transactionHash.array() )
                {
                    m_replicator.removeModifyDriveInfo(m_deferredModificationRequests.front()->m_transactionHash.array());
                }
                m_opinionController.increaseApprovedExpectedCumulativeDownload(m_deferredModificationRequests.front()->m_maxDataSize);
                m_deferredModificationRequests.pop_front();
            }

            _ASSERT( !m_deferredModificationRequests.empty() )

            if ( opinionTrafficIdentifier &&
            *opinionTrafficIdentifier != m_deferredModificationRequests.front()->m_transactionHash.array() )
            {
                m_replicator.removeModifyDriveInfo(m_deferredModificationRequests.front()->m_transactionHash.array());
            }
            m_opinionController.increaseApprovedExpectedCumulativeDownload(m_deferredModificationRequests.front()->m_maxDataSize);
            m_deferredModificationRequests.pop_front();
        }

        m_task = createCatchingUpTask( std::move(m_catchingUpRequest), *this, m_opinionController );

        _ASSERT ( !m_catchingUpRequest )

        _ASSERT( m_task->getTaskType() == DriveTaskType::CATCHING_UP )

        m_task->run();
    }

    void runCancelModificationTask()
    {
        DBG_MAIN_THREAD

        _ASSERT( !m_task )

        m_task = createModificationCancelTask( std::move(m_modificationCancelRequest), *this, m_opinionController );

        _ASSERT( m_task->getTaskType() == DriveTaskType::MODIFICATION_CANCEL )

        m_task->run();
    }

    void runCloseDriveTask()
    {
        DBG_MAIN_THREAD

        _ASSERT( !m_task )

        m_task = createDriveClosureTask( std::move(m_closeDriveRequest), *this );

        _ASSERT( m_task->getTaskType() == DriveTaskType::DRIVE_CLOSURE )

        m_task->run();
    }

    void runVerificationTask()
    {
        auto receivedOpinions = std::move(m_unknownVerificationOpinions[m_deferredVerificationRequest->m_tx]);
        m_unknownVerificationOpinions.erase(m_deferredVerificationRequest->m_tx);

        auto receivedCodes = std::move(m_unknownVerificationCodeQueue[m_deferredVerificationRequest->m_tx]);
        m_unknownVerificationOpinions.erase(m_deferredVerificationRequest->m_tx);

        m_verificationTask = createDriveVerificationTask( std::move(m_deferredVerificationRequest), std::move(receivedOpinions), std::move(receivedCodes), *this);
        m_verificationTask->run();
    }


    //
    // CLOSE/REMOVE
    //

    void startVerification( mobj<VerificationRequest>&& request ) override
    {
        DBG_MAIN_THREAD

        m_deferredVerificationRequest = std::move(request);

        if ( m_task && m_task->getTaskType() == DriveTaskType::DRIVE_INITIALIZATION )
        {
            return;
        }

        if ( m_verificationTask )
        {
            _LOG_ERR("startVerification: internal error: m_verificationRequest != null")
        }
        
        if (m_deferredVerificationRequest->m_actualRootHash == m_rootHash )
        {
            runVerificationTask();
        }
    }

    /// Information Received from Other Replicators

    void onOpinionReceived( mobj<ApprovalTransactionInfo>&& anOpinion ) override
    {
        DBG_MAIN_THREAD
        
        // Preliminary opinion verification takes place at extension

        if ( !m_task || !m_task->processedModifyOpinion( *anOpinion ))
        {
            m_unknownModificationOpinions[anOpinion->m_modifyTransactionHash][anOpinion->m_opinions[0].m_replicatorKey] = *anOpinion;
        }
    }

    void onVerificationOpinionReceived( mobj<VerifyApprovalTxInfo>&& anOpinion ) override
    {
        DBG_MAIN_THREAD

        // Preliminary opinion verification takes place at extension

        if ( !m_verificationTask || !m_verificationTask->processedVerificationOpinion( *anOpinion ))
        {
            m_unknownVerificationOpinions[anOpinion->m_tx].push_back(*anOpinion);
        }
    }

    void onVerificationCodeReceived( mobj<VerificationCodeInfo>&& code ) override
    {
        DBG_MAIN_THREAD

        // Preliminary opinion verification takes place at extension

        if ( !m_verificationTask || !m_verificationTask->processedVerificationCode( *code ))
        {

            m_unknownVerificationCodeQueue[code->m_tx].push_back(*code);
        }
    }

    ////////////

    /// Transactions from Replicators

    void onApprovalTransactionHasBeenPublished( const PublishedModificationApprovalTransactionInfo& transaction ) override
    {
        DBG_MAIN_THREAD

        _LOG ( "approval has been published" << Hash256{transaction.m_modifyTransactionHash} )

        if ( m_verificationTask )
        {
            _ASSERT ( !m_verificationTask->shouldCatchUp(transaction) )
        }

        if ( !m_task || m_task->shouldCatchUp( transaction ) )
        {
            m_catchingUpRequest = mobj<CatchingUpRequest>{transaction.m_rootHash, transaction.m_modifyTransactionHash };

            if ( !m_task )
            {
                runNextTask();
            }
        }
    }

    void onApprovalTransactionHasFailedInvalidOpinions( const Hash256& transactionHash ) override
    {
        DBG_MAIN_THREAD

        if ( m_task )
        {
            m_task->approvalTransactionHasFailed( transactionHash );
        }
    }

    void onSingleApprovalTransactionHasBeenPublished( const PublishedModificationSingleApprovalTransactionInfo& transaction ) override
    {
        _LOG( "onSingleApprovalTransactionHasBeenPublished()" );
    }


    void onVerifyApprovalTransactionHasBeenPublished( PublishedVerificationApprovalTransactionInfo info ) override
    {
        DBG_MAIN_THREAD

        if ( !m_verificationTask )
        {
            _LOG_ERR( "verifyApprovalPublished: internal error: m_verificationRequest == null" )
            return;
        }

        m_verificationTask->cancelVerification( info.m_tx );
        m_verificationTask.reset();
    }

    ////////////

    /// Transactions NOT from Replicators

    // startModifyDrive - should be called after client 'modify request'
    //

    void startModifyDrive( mobj<ModificationRequest>&& modifyRequest ) override
    {
        DBG_MAIN_THREAD

        _LOG ( "started modification " << Hash256{modifyRequest->m_transactionHash} )

        // ModificationIsCanceling check is redundant now
        m_deferredModificationRequests.push_back(std::move(modifyRequest) );

        if ( !m_task )
        {
            runNextTask();
        }
    }

    void cancelModifyDrive( mobj<ModificationCancelRequest>&& request ) override
    {
        DBG_MAIN_THREAD

        if ( m_task && m_task->shouldCancelModify(*request) )
        {
            m_modificationCancelRequest = request;
        }

        else
        {
            auto it = std::find_if(m_deferredModificationRequests.begin(), m_deferredModificationRequests.end(), [&request](const auto& item)
            { return item->m_transactionHash == request->m_modifyTransactionHash; });

            if (it == m_deferredModificationRequests.end() )
            {
                _LOG_ERR( "cancelModifyDrive(): invalid transactionHash: " << request->m_modifyTransactionHash );
                return;
            }

            m_deferredModificationRequests.erase( it );
        }
    }

    void startDriveClosing( mobj<DriveClosureRequest>&& request ) override
    {
        DBG_MAIN_THREAD

        m_closeDriveRequest = request;

        if ( !m_task )
        {
            runNextTask();
        }
    }

    void cancelVerification( mobj<Hash256>&& tx ) override
    {
        DBG_MAIN_THREAD

        if ( !m_verificationTask )
        {
            _LOG_ERR( "cancelVerification: internal error: m_verificationRequest == null" )
            return;
        }

        m_verificationTask->cancelVerification( *tx );
        m_verificationTask.reset();
    }

    void  addShardDonator( mobj<Key>&& replicatorKey ) override
    {
        auto it = std::find( m_modifyDonatorShard.begin(), m_modifyDonatorShard.end(), *replicatorKey );
        if ( it != m_modifyDonatorShard.end() )
        {
            _LOG_WARN( "duplicated key" << Key(*replicatorKey) )
            return;
        }
        
        m_modifyDonatorShard.push_back( *replicatorKey );
    }
    
    void  removeShardDonator( mobj<Key>&& replicatorKey ) override
    {
        auto it = std::find( m_modifyDonatorShard.begin(), m_modifyDonatorShard.end(), *replicatorKey );
        if ( it == m_modifyDonatorShard.end() )
        {
            _LOG_WARN( "unknown key" << Key(*replicatorKey) )
            return;
        }
        
        m_modifyDonatorShard.erase( it );
    }
    
    void  addShardRecipient( mobj<Key>&& replicatorKey ) override
    {
        auto it = std::find( m_modifyRecipientShard.begin(), m_modifyRecipientShard.end(), *replicatorKey );
        if ( it != m_modifyRecipientShard.end() )
        {
            _LOG_WARN( "duplicated key" << Key(*replicatorKey) )
            return;
        }
        
        m_modifyRecipientShard.push_back( *replicatorKey );
    }
    
    void  removeShardRecipient( mobj<Key>&& replicatorKey ) override
    {
        auto it = std::find( m_modifyRecipientShard.begin(), m_modifyRecipientShard.end(), *replicatorKey );
        if ( it == m_modifyRecipientShard.end() )
        {
            _LOG_WARN( "unknown key" << Key(*replicatorKey) )
            return;
        }
        
        m_modifyRecipientShard.erase( it );
    }

    ////////////

    bool isOutOfSync() const override
    {
        return m_task && m_task->getTaskType() == DriveTaskType::CATCHING_UP;
    }

    bool isItClosingTxHash( const Hash256& eventHash ) const override
    {
        DBG_MAIN_THREAD
        return m_closeDriveRequest && (m_closeDriveRequest->m_removeDriveTx == eventHash);
    }

    void dbgPrintDriveStatus() override
    {
        LOG("Drive Status:")
        m_fsTree->dbgPrint();
        if ( auto session = m_session.lock(); session )
        {
            session->printActiveTorrents();
        }
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

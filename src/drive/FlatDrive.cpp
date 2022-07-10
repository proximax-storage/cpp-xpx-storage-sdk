/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "drive/FlatDrive.h"
#include "drive/Replicator.h"
#include "drive/Session.h"
#include "DriveTaskBase.h"
#include "DriveParams.h"
#include "ModifyOpinionController.h"
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

#undef DBG_MAIN_THREAD
//#define DBG_MAIN_THREAD { assert( m_dbgThreadId == std::this_thread::get_id() ); }
#define DBG_MAIN_THREAD { _FUNC_ENTRY(); assert( m_dbgThreadId == std::this_thread::get_id() ); }
#define DBG_BG_THREAD { assert( m_dbgThreadId != std::this_thread::get_id() ); }
#define DBG_VERIFY_THREAD { assert( m_verifyThread.get_id() == std::this_thread::get_id() ); }

//
// DrivePaths - drive paths, used at replicator side
//

std::string FlatDrive::driveIsClosingPath( const std::string& driveRootPath )
{
    return (fs::path(driveRootPath) / "restart-data" / "drive-is-closing").string();
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
class DefaultFlatDrive: public FlatDrive, public DriveParams
{
    // List of all replicators that support this drive
    // (It does not contain our replicator key!)
    ReplicatorList m_allReplicators;

    ReplicatorList m_modifyDonatorShard;
    ReplicatorList m_modifyRecipientShard;

    // Opinion Controller
    ModifyOpinionController  m_opinionController;

    // opinions from other replicators
    // key of the outer map is modification id
    // key of the inner map is a replicator key, one replicator one opinion
    //
    std::map<Hash256, std::map<std::array<uint8_t,32>,ApprovalTransactionInfo>>     m_unknownModificationOpinions;
    std::map<Hash256, std::vector<VerifyApprovalTxInfo>>                            m_unknownVerificationOpinions;
    std::map<Hash256, std::vector<VerificationCodeInfo>>                            m_unknownVerificationCodeQueue;

    //
    // Request queue
    //

    mobj<DriveClosureRequest>               m_closeDriveRequest = {};
    mobj<ModificationCancelRequest>         m_modificationCancelRequest;
    mobj<CatchingUpRequest>                 m_catchingUpRequest;
    
    struct DeferredRequest
    {
        mobj<ModificationRequest> m_modificationRequest;
        mobj<StreamRequest>       m_streamRequest;

        const Hash256& transactionHash() const
        {
            if ( m_modificationRequest )
            {
                return m_modificationRequest->m_transactionHash;
            }
            __ASSERT( m_streamRequest )
            return m_streamRequest->m_streamId;
        }
        
        uint64_t maxDataSize()
        {
            if ( m_modificationRequest )
            {
                return m_modificationRequest->m_maxDataSize;
            }
            __ASSERT( m_streamRequest )
            return m_streamRequest->m_maxSizeBytes;
        }
    };
    std::deque<DeferredRequest>             m_deferredModificationRequests;
    
    mobj<VerificationRequest>               m_deferredVerificationRequest;

    // Executing Drive Tasks
    std::unique_ptr<DriveTaskBase> m_task;
    std::shared_ptr<DriveTaskBase> m_verificationTask;

    std::set<Key>                  m_blockedReplicators; // blocked until verification will be approved

public:

    DefaultFlatDrive(
                std::shared_ptr<Session>    session,
                const std::string&          replicatorRootFolder,
                const std::string&          replicatorSandboxRootFolder,
                const Key&                  drivePubKey,
                const Key&                  driveOwner,
                size_t                      maxSize,
                size_t                      expectedCumulativeDownload,
                ReplicatorEventHandler&     eventHandler,
                ReplicatorInt&                 replicator,
                const ReplicatorList&       fullReplicatorList,
                const ReplicatorList&       modifyDonatorShard,
                const ReplicatorList&       modifyRecipientShard,
                DbgReplicatorEventHandler*  dbgEventHandler
            )
            : DriveParams(
                    drivePubKey,
                    driveOwner,
                    maxSize,
                    session,
                    eventHandler,
                    replicator,
                    dbgEventHandler,
                    replicatorRootFolder,
                    replicatorSandboxRootFolder,
                    replicator.dbgReplicatorName()
              )
            , m_allReplicators(fullReplicatorList)
            , m_modifyDonatorShard(modifyDonatorShard)
            , m_modifyRecipientShard(modifyRecipientShard)
            //(???+)
            , m_opinionController(m_driveKey, m_driveOwner, m_replicator, m_serializer, *this, expectedCumulativeDownload, replicator.dbgReplicatorName() )

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

    const ReplicatorList& getDonatorShard() const override {
        return m_modifyDonatorShard;
    }

    const Key& driveOwner() const override
    {
        return m_driveOwner;
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
        m_replicator.executeOnBackgroundThread( task );
    }

    void runNextTask() override
    {
        DBG_MAIN_THREAD

        if (m_replicator.isStopped())
        {
            return;
        }

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
            runDeferredModificationTask();
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

    void runDeferredModificationTask()
    {
        DBG_MAIN_THREAD

        _ASSERT( !m_task )

        DeferredRequest request{ std::move( m_deferredModificationRequests.front().m_modificationRequest ),
                                 std::move( m_deferredModificationRequests.front().m_streamRequest ) };
        m_deferredModificationRequests.pop_front();
        
        if ( request.m_modificationRequest )
        {
            runModificationTask( std::move( request.m_modificationRequest ) );
        }
        else
        {
            __ASSERT( request.m_streamRequest )
            runStreamTask( std::move( request.m_streamRequest ) );
        }
    }

    void runModificationTask( mobj<ModificationRequest>&& request )
    {
        DBG_MAIN_THREAD

        _ASSERT( !m_task )

        auto opinions = std::move(m_unknownModificationOpinions[request->m_transactionHash]);
        m_unknownModificationOpinions.erase(request->m_transactionHash);

        m_task = createModificationTask( std::move(request), std::move(opinions), *this, m_opinionController );

        _ASSERT( m_task->getTaskType() == DriveTaskType::MODIFICATION_REQUEST )

        m_task->run();
    }

    void runStreamTask( mobj<StreamRequest>&& request )
    {
        DBG_MAIN_THREAD

        _ASSERT( !m_task )

//        auto opinions = std::move(m_unknownModificationOpinions[request->m_transactionHash]);
//        m_unknownModificationOpinions.erase(request->m_transactionHash);

        m_task = createStreamTask( std::move(request), *this, m_opinionController );

        _ASSERT( m_task->getTaskType() == DriveTaskType::STREAM_REQUEST )

        m_task->run();
    }

    void runCatchingUpTask()
    {
        DBG_MAIN_THREAD

        _ASSERT( !m_task )

        // clear modification queue - we will not execute these modifications
        auto it = std::find_if( m_deferredModificationRequests.begin(), m_deferredModificationRequests.end(), [&]( const auto& item )
            {
                return item.transactionHash() == m_catchingUpRequest->m_modifyTransactionHash;
            } );
        
        if (it != m_deferredModificationRequests.end() )
        {
            const auto& opinionTrafficIdentifier = m_opinionController.opinionTrafficTx();
            while (!m_deferredModificationRequests.empty() and it != m_deferredModificationRequests.begin() )
            {
                if ( !opinionTrafficIdentifier
                || *opinionTrafficIdentifier != m_deferredModificationRequests.front().transactionHash().array() )
                {
                    m_replicator.removeModifyDriveInfo( m_deferredModificationRequests.front().transactionHash().array() );
                }
                m_opinionController.increaseApprovedExpectedCumulativeDownload( m_deferredModificationRequests.front().maxDataSize() );
                m_deferredModificationRequests.pop_front();
            }

            _ASSERT( !m_deferredModificationRequests.empty() )

            if ( opinionTrafficIdentifier &&
            *opinionTrafficIdentifier != m_deferredModificationRequests.front().transactionHash().array() )
            {
                m_replicator.removeModifyDriveInfo(m_deferredModificationRequests.front().transactionHash().array());
            }
            m_opinionController.increaseApprovedExpectedCumulativeDownload( m_deferredModificationRequests.front().maxDataSize() );
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
        DBG_MAIN_THREAD

        auto receivedOpinions = std::move(m_unknownVerificationOpinions[m_deferredVerificationRequest->m_tx]);
        m_unknownVerificationOpinions.erase(m_deferredVerificationRequest->m_tx);

        auto receivedCodes = std::move(m_unknownVerificationCodeQueue[m_deferredVerificationRequest->m_tx]);
        m_unknownVerificationOpinions.erase(m_deferredVerificationRequest->m_tx);

        m_blockedReplicators = std::move(m_deferredVerificationRequest->m_blockedReplicators);
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

        _LOG ( "Received Verification Request " << m_deferredVerificationRequest->m_actualRootHash << " " << m_rootHash );

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

        m_blockedReplicators.clear();

        if ( m_verificationTask )
        {
            //(???+++) replace by? m_verificationTask->terminate();
            m_verificationTask->onApprovalTxPublished(transaction);
            m_verificationTask.reset();
        }

        // Notify task about 'ApprovalTxPublished'
        // and check that 'CatchingUp' should be started
        //
        if ( !m_task || m_task->onApprovalTxPublished( transaction ) )
        {
            // 'CatchingUp' should be started
            _LOG( "transaction.m_rootHash: " << Key(transaction.m_rootHash) )
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
            m_task->onApprovalTxFailed( transactionHash );
        }
    }

    void onSingleApprovalTransactionHasBeenPublished( const PublishedModificationSingleApprovalTransactionInfo& transaction ) override
    {
        _LOG( "onSingleApprovalTransactionHasBeenPublished()" );
    }


    void onVerifyApprovalTransactionHasBeenPublished( PublishedVerificationApprovalTransactionInfo info ) override
    {
        DBG_MAIN_THREAD

        m_blockedReplicators.clear();

        if ( !m_verificationTask )
        {
            _LOG_ERR( "verifyApprovalPublished: internal error: m_verificationRequest == null" )
            return;
        }

        m_verificationTask->cancelVerification();
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
        m_deferredModificationRequests.push_back( DeferredRequest{ std::move(modifyRequest), {} } );

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
            { return item.transactionHash() == request->m_modifyTransactionHash; });

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

        cancelVerification();

        if ( !m_task )
        {
            runNextTask();
        }
        else
        {
            m_task->onDriveClose( *request );
        }
    }

    void cancelVerification() override
    {
        DBG_MAIN_THREAD

        m_blockedReplicators.clear();

        if ( !m_verificationTask )
        {
            return;
        }

        m_verificationTask->cancelVerification();
        m_verificationTask.reset();
    }
    
    void  startStream( mobj<StreamRequest>&& streamRequest ) override
    {
        DBG_MAIN_THREAD

        _LOG ( "start streaming: " << Hash256{streamRequest->m_streamId} )

        m_deferredModificationRequests.push_back( DeferredRequest{ {}, std::move(streamRequest) } );

        if ( ! m_task )
        {
            runNextTask();
        }
    }
    
    void  increaseStream( mobj<StreamIncreaseRequest>&& ) override
    {
        
    }
    
    void  finishStream( mobj<StreamFinishRequest>&& ) override
    {
        
    }

    void setReplicators( mobj<ReplicatorList>&& replicatorKeys ) override
    {
		m_allReplicators = *replicatorKeys;
	}

    void setShardDonator( mobj<ReplicatorList>&& replicatorKeys ) override
    {
    	m_modifyDonatorShard = *replicatorKeys;

		std::set<Key> replicators = {m_allReplicators.begin(), m_allReplicators.end()};

		for ( const auto& key: m_modifyDonatorShard )
		{
			if ( replicators.find(key) == replicators.end() )
			{
				_LOG_ERR( "Unknown Replicator Added to Shard Donator" )
			}
		}
	}

	void setShardRecipient( mobj<ReplicatorList>&& replicatorKeys ) override
	{
    	m_modifyRecipientShard = *replicatorKeys;

    	std::set<Key> replicators = {m_allReplicators.begin(), m_allReplicators.end()};

    	for ( const auto& key: m_modifyRecipientShard )
    	{
    		if ( replicators.find(key) == replicators.end() )
    		{
    			_LOG_ERR( "Unknown Replicator Added to Shard Recipient" )
    		}
    	}
	}

    const ReplicatorList& donatorShard() const override { return m_modifyDonatorShard; }

    bool acceptConnectionFromReplicator( const Key& replicatorKey ) const override
    {
        if ( m_blockedReplicators.size() > 0 )
        {
            if ( (m_task && m_task->getTaskType() == DriveTaskType::MODIFICATION_REQUEST) ||
                m_deferredModificationRequests.size() > 0 )
            {
                // skip 'm_blockedReplicators' check
            }
            else
            {
                if ( std::find( m_blockedReplicators.begin(), m_blockedReplicators.end(), replicatorKey )
                        != m_blockedReplicators.end() )
                {
					_LOG_WARN( "Connection From Blocked Replicator " << replicatorKey )
                    return false;
                }
            }
        }

		if (std::find(m_modifyRecipientShard.begin(), m_modifyRecipientShard.end(), replicatorKey) ==
			m_modifyRecipientShard.end())
		{
			_LOG_WARN( "Connection From Not Shard Replicator " << replicatorKey )
			return false;
		}

		return true;
	}

    void acceptChunkInfoMessage( mobj<ChunkInfo>&& chunkInfo, const boost::asio::ip::udp::endpoint& streamer ) override
    {
        DBG_MAIN_THREAD
        
        if ( m_task )
        {
            m_task->acceptChunkInfoMessage( std::move(chunkInfo), streamer );
        }
    }

    void acceptFinishStreamMessage( mobj<FinishStream>&& finishStream, const boost::asio::ip::udp::endpoint& streamer ) override
    {
        DBG_MAIN_THREAD
        
        if ( m_task )
        {
            m_task->acceptFinishStreamMessage( std::move(finishStream), streamer );
        }
    }

    std::string acceptGetChunksInfoMessage( uint32_t                               chunkIndex,
                                            const boost::asio::ip::udp::endpoint&  viewer ) override
    {
        DBG_MAIN_THREAD
        
        if ( m_task )
        {
            return m_task->acceptGetChunksInfoMessage( chunkIndex, viewer );
        }
        
        return {};
    }
    ////////////

    void dbgPrintDriveStatus() override
    {
        LOG("Drive Status:")
        m_fsTree->dbgPrint();
        if ( auto session = m_session.lock(); session )
        {
            session->dbgPrintActiveTorrents();
        }
    }
    
    virtual void dbgAsyncDownloadToSandbox( InfoHash infoHash, std::function<void()> endNotifyer ) override
    {
        if ( auto session = m_session.lock(); session )
        {
            static std::array<uint8_t,32> streamTx  = std::array<uint8_t,32>{0xee,0xee,0xee,0xee};

            session->download(
                   DownloadContext(
                           DownloadContext::missing_files,
                           [=,this]( download_status::code code,
                                   const InfoHash& infoHash,
                                   const std::filesystem::path saveAs,
                                   size_t /*downloaded*/,
                                   size_t /*fileSize*/,
                                   const std::string& errorText )
                           {
                               DBG_MAIN_THREAD

                               if ( code == download_status::dn_failed )
                               {
                                   //todo is it possible?
                                   _ASSERT( 0 );
                                   return;
                               }

                               if ( code == download_status::download_complete )
                               {
                                   endNotifyer();
                               }
                           },
                           infoHash,
                           *m_opinionController.opinionTrafficTx(),
                           0, true, ""
                   ),
                   m_sandboxRootPath.string(),
                   (m_sandboxRootPath / toString(infoHash)).string(),
                   {},
                   &m_driveKey.array(),
                   nullptr,
                   &streamTx );
        }
    }
    
    
    //-----------------------------------------------------------------------------
};


std::shared_ptr<FlatDrive> createDefaultFlatDrive(
        std::shared_ptr<Session>    session,
        const std::string&          replicatorRootFolder,
        const std::string&          replicatorSandboxRootFolder,
        const Key&                  drivePubKey,
        const Key&                  clientPubKey,
        size_t                      maxSize,
        size_t                      usedDriveSizeExcludingMetafiles,
        ReplicatorEventHandler&     eventHandler,
        Replicator&                 replicator,
        const ReplicatorList&       fullReplicatorList,
        const ReplicatorList&       modifyDonatorShard,
        const ReplicatorList&       modifyRecipientShard,
        DbgReplicatorEventHandler*  dbgEventHandler )

{
    return std::make_shared<DefaultFlatDrive>( session,
                                           replicatorRootFolder,
                                           replicatorSandboxRootFolder,
                                           drivePubKey,
                                           clientPubKey,
                                           maxSize,
                                           usedDriveSizeExcludingMetafiles,
                                           eventHandler,
                                           dynamic_cast<ReplicatorInt&>(replicator),
                                           fullReplicatorList,
                                           modifyDonatorShard,
                                           modifyRecipientShard,
                                           dbgEventHandler );
}

}

/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "DownloadLimiter.h"
#include "DriveTaskBase.h"
#include "drive/FlatDrive.h"
#include "drive/FsTree.h"
#include "drive/ActionList.h"

#include <cereal/types/vector.hpp>
#include <cereal/types/array.hpp>
#include <cereal/types/map.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/archives/portable_binary.hpp>

namespace sirius::drive
{

class VerificationDriveTask
        : public DriveTaskBase
        , public std::enable_shared_from_this<VerificationDriveTask>
{

private:

    mobj<VerificationRequest>                                   m_request;

    std::thread                                                 m_verifyThread;

    bool                                                        m_myVerifyCodesCalculated;
	bool														m_codeTimerRun;
    bool                                                        m_verifyApproveTxSent;
    std::optional<boost::posix_time::ptime>                     m_verificationStartedAt;

    std::vector<uint64_t>                                       m_verificationCodes;

    Timer                                                       m_verifyCodeTimer;
    Timer                                                       m_verifyOpinionTimer;
    Timer                                                       m_shareVerifyCodeTimer;
    Timer                                                       m_shareVerifyOpinionTimer;

    std::optional<VerificationCodeInfo>                         m_myCodeInfo;
    std::optional<VerifyApprovalTxInfo>                         m_myOpinion;

    std::map<std::array<uint8_t,32>, VerificationCodeInfo>      m_receivedCodes;
    std::optional<VerifyApprovalTxInfo>                         m_myVerificationApprovalTxInfo;

    // Verification will be canceled, after any modify tx has been published
    std::atomic_bool                                            m_verificationMustBeInterrupted;

public:

    VerificationDriveTask(
            mobj<VerificationRequest>&& request,
            std::vector<VerifyApprovalTxInfo>&& receivedOpinions,
            std::vector<VerificationCodeInfo>&& receivedCodes,
            DriveParams& drive)
            : DriveTaskBase(DriveTaskType::DRIVE_VERIFICATION, drive)
            , m_request(request)
            , m_myVerifyCodesCalculated(false)
			, m_codeTimerRun(false)
            , m_verifyApproveTxSent(false)
            , m_verificationMustBeInterrupted(false)
    {
        // add unknown opinions in 'myVerifyApprovalTxInfo'
        for ( const auto& opinion: receivedOpinions )
        {
            _ASSERT( processedVerificationOpinion(opinion) )
        }

        // Add early received 'verification codes' from other replicators
        for ( const auto& verifyCode: receivedCodes )
        {
            _ASSERT ( processedVerificationCode( verifyCode ) )
        }
    }

    void run() override
    {
        DBG_MAIN_THREAD

        _LOG( "Started Verification " << m_request->m_tx )

        m_verificationStartedAt = boost::posix_time::microsec_clock::universal_time();

        //
        // Run verification task on separate thread
        //
        m_verifyThread = std::thread( [t = weak_from_this()]
        {
            if ( auto task = t.lock(); task )
            {
                task->verify();
            }
        } );
    }

    void terminate() override
    {
        DBG_MAIN_THREAD

        _LOG( "Verification Is Terminated " << m_request->m_tx )

        m_verifyCodeTimer.cancel();
        m_verifyOpinionTimer.cancel();
        m_shareVerifyCodeTimer.cancel();
        m_shareVerifyOpinionTimer.cancel();

        m_verificationMustBeInterrupted = true;

        if ( m_verifyThread.joinable() )
        {
            try {
                m_verifyThread.join();
            }
            catch(...) {
                _LOG_ERR( "Failed Joining Thread" );
            }
        }
    }

    bool onApprovalTxPublished( const PublishedModificationApprovalTransactionInfo& transaction ) override
    {
        terminate();

        // Return result will be ignored
        return false;
    }

    bool shouldCancelModify( const ModificationCancelRequest& cancelRequest ) override
    {
        return false;
    }

    void onDriveClose( const DriveClosureRequest& closureRequest ) override
    {

    }

    bool processedVerificationCode( const VerificationCodeInfo& info ) final
    {
        DBG_MAIN_THREAD

        _LOG( "processed verification code from "  << int(info.m_replicatorKey[0]) )

        // Save verification opinion in queue, if we so far does not received verificationRequest
        if ( info.m_tx != m_request->m_tx.array() )
        {
            _LOG( "processVerificationCode: unknown tx: " << Key(info.m_tx) )

            return false;
        }

        //
        // Check replicator key, it must be in replicatorList
        //
        auto& replicatorKeys = m_request->m_replicators;
        auto keyIt = std::find_if( replicatorKeys.begin(), replicatorKeys.end(), [&key=info.m_replicatorKey] (const auto& it) {
            return it.array() == key;
        });

        if ( keyIt == replicatorKeys.end() )
        {
            _LOG_WARN( "processVerificationCode: unknown replicatorKey" << Key(info.m_replicatorKey) );
            return true;
        }

        if ( m_receivedCodes.contains(info.m_replicatorKey) )
        {
            _LOG( "Ignoring Duplicate Verification Code" );
            return true;
        }

        m_receivedCodes[info.m_replicatorKey] = info;

        if ( m_myVerifyCodesCalculated )
        {
            checkVerifyCodeNumber();
        }

        return true;
    }

    bool processedVerificationOpinion( const VerifyApprovalTxInfo& info ) final
    {
        DBG_MAIN_THREAD

        _LOG( "processed verification opinion from "  << int(info.m_opinions[0].m_publicKey[0]) )

        _ASSERT( info.m_opinions.size() == 1 )

        if ( m_request->m_tx != info.m_tx )
        {
            // opinion is old or new
            _LOG( "processVerificationOpinion: received unknown verification opinion: " << Hash256(info.m_tx) )
            return false;
        }

        if ( m_request->m_shardId != info.m_shardId )
        {
            _LOG_WARN( "processVerificationOpinion: received opinion with different m_shardId: " << info.m_shardId << " vs " << m_request->m_shardId )
            return true;
        }

        // Check sender replicator key
        //
        const auto& keys = m_request->m_replicators;
        bool isUnexpected = std::find_if( keys.begin(), keys.end(), [&info] (const auto& it) {
            return it.array() == info.m_opinions[0].m_publicKey;
        }) == keys.end();

        if ( isUnexpected )
        {
            _LOG_WARN( "processVerificationOpinion: received opinion from unexpected replicator: " << Key(info.m_opinions[0].m_publicKey) )
            return true;
        }

        if ( info.m_opinions[0].m_opinions.size() != m_request->m_replicators.size() )
        {
            _LOG_WARN( "processVerificationOpinion: incorrect number of replicators in opinion: " << info.m_opinions[0].m_opinions.size() << " vs " << m_request->m_replicators.size() )
            return true;
        }

        if ( !m_myVerificationApprovalTxInfo )
        {
            m_myVerificationApprovalTxInfo = info;
        }

        _ASSERT( m_myVerificationApprovalTxInfo->m_tx == info.m_tx )

        // At any case opinions with the same replicator key must be removed
        //
        auto& opinions = m_myVerificationApprovalTxInfo->m_opinions;

        auto it = std::find_if( opinions.begin(), opinions.end(), [&info]( const auto& it )
        {
            return it.m_publicKey == info.m_opinions[0].m_publicKey;
        } );

        if ( it != opinions.end() )
        {
            _LOG( "Ignoring Duplicate Verification Opinion" );
            return true;
        }

        m_myVerificationApprovalTxInfo->m_opinions.emplace_back( info.m_opinions[0] );

        if ( m_myOpinion && m_myVerificationApprovalTxInfo->m_opinions.size() > (m_request->m_replicators.size() * 2 ) / 3 )
        {
            // start timer if it is not started
            if ( !m_verifyOpinionTimer )
            {
                if ( auto session = m_drive.m_session.lock(); session )
                {
                    m_verifyOpinionTimer = session->startTimer( m_drive.m_replicator.getVerifyApprovalTransactionTimerDelay(),
                                                                [this]() {
                    	verifyOpinionTimerExpired();
                    } );
                }
            }
        }
        return true;
    }

    void cancelVerification() override
    {
        terminate();
    }


private:

    void verify()
    {
        // commented because of races
        //            DBG_VERIFY_THREAD

        _LOG( "Started Verify Thread " << m_request->m_tx )

        m_verificationCodes = std::vector<uint64_t>(
                m_request->m_replicators.size(), 0 );

        for ( uint32_t i = 0; i < m_verificationCodes.size(); i++ )
        {
            uint64_t initHash = calcHash64( 0, m_request->m_tx.begin(),
                                            m_request->m_tx.end());
            initHash = calcHash64( initHash,
                                   m_request->m_replicators[i].begin(),
                                   m_request->m_replicators[i].end());
            m_verificationCodes[i] = initHash;
        }

		_LOG( "Started Calculating Verification Codes " << m_request->m_tx );

        calculateVerifyCodes( *m_drive.m_fsTree );

        _LOG( "Finished Calculating Verification Codes " << m_request->m_tx );

        m_drive.executeOnSessionThread( [t = weak_from_this()]
        {
            if ( auto task = t.lock(); task )
            {
                task->verificationCodesCompleted();
            }
        } );
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
            hash = ( hash >> 1 ) | ( hash << 63 );
        }

        // At the end, we process tail
        uint64_t lastValue = 0;
        for( ; ptr < end; ptr++ )
        {
            lastValue = lastValue << 8;
            lastValue |= *ptr;
        }

        hash ^= lastValue;
        hash = ( hash >> 1 ) | ( hash << 63 );

        return hash;
    }

    void calculateVerifyCodes( const Folder& folder )
    {
        //        DBG_VERIFY_THREAD

        _LOG( "Calculate Verify Codes" )

        for( const auto& child : folder.childs() )
        {
            if ( m_verificationMustBeInterrupted )
            {
                break;
            }

            if ( isFolder(child) )
            {
                _LOG( "Calculate Verify Codes Folder " << getFolder(child).name() )
                calculateVerifyCodes( getFolder(child) );
            }
            else
            {
                _LOG( "Calculate Verify Codes File " << getFile(child).name() )
                const auto& fileHash = getFile(child).hash();
                std::string fileName = (m_drive.m_driveFolder / toString(fileHash)).string();

                std::error_code err;
                if ( !fs::exists( fileName, err ) )
                {
                    _LOG_ERR( "calculateVerifyCodes: file is absent: " << toString(fileHash) );
                    //TODO maybe break?
                    return;
                }

                if (err) {
                    _LOG_ERR( err.message() );
                }

                uint8_t buffer[4096];
                FILE* file = fopen( fileName.c_str(), "rb" );

                if ( file == nullptr ) {
                    _LOG_ERR( "Cannot open the file: " << fileName );
                }

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

    void verificationCodesCompleted()
    {
        DBG_MAIN_THREAD

        _LOG( "Verification Codes Completed " << m_request->m_tx )

        if ( m_verificationMustBeInterrupted )
        {
            // 'Verify Approval Tx' already published or canceled (we are late)
            return;
        }

        m_myVerifyCodesCalculated = true;

        //
        // Get our key and verification code
        //
        const auto& replicators = m_request->m_replicators;
        const auto& ourKey = m_drive.m_replicator.dbgReplicatorKey();
        auto keyIt = std::find_if( replicators.begin(), replicators.end(), [&ourKey] (const auto& it) {
            return it == ourKey;
        });
        auto ourIndex = std::distance( replicators.begin(), keyIt );
        uint64_t ourCode = m_verificationCodes[ ourIndex ];

        //
        // Prepare message
        //
        m_myCodeInfo = { m_request->m_tx.array(), ourKey.array(), m_drive.m_driveKey.array(), ourCode, {} };
        m_myCodeInfo->Sign( m_drive.m_replicator.keyPair() );

        shareVerifyCode();

        checkVerifyCodeNumber();
    }

    void checkVerifyCodeNumber()
    {
        DBG_MAIN_THREAD

        _LOG( "Check Verify Code Number " << m_request->m_tx )

        _ASSERT( m_myVerifyCodesCalculated )

        auto replicatorNumber = m_request->m_replicators.size();

        // check code number
        if ( m_receivedCodes.size() == replicatorNumber-1 )
        {
            m_verifyCodeTimer.cancel();
            verifyCodeTimerExpired();
        }
        else if ( !m_codeTimerRun )
		{
			m_codeTimerRun = true;

			_ASSERT( m_verificationStartedAt )

			auto secondsSinceVerificationStart =
					(boost::posix_time::microsec_clock::universal_time() - *m_verificationStartedAt).total_seconds();
			int codesDelay;
			if ( m_request->m_durationMs > secondsSinceVerificationStart + m_drive.m_replicator.getVerifyCodeTimerDelay() )
			{
				codesDelay = int(m_request->m_durationMs - secondsSinceVerificationStart + m_drive.m_replicator.getVerifyCodeTimerDelay());
			}
			else
			{
				codesDelay = 0;
			}

			if ( auto session = m_drive.m_session.lock(); session )
			{
                m_verifyCodeTimer = session->startTimer( codesDelay, [this]()
                {
                	verifyCodeTimerExpired();
                } );
			}
        }
    }

    void verifyOpinionTimerExpired()
    {
        DBG_MAIN_THREAD

        _LOG( "Verify Opinion Timer Expired " << m_request->m_tx )

        _ASSERT( !m_verificationMustBeInterrupted )

        m_verifyApproveTxSent = true;
        m_verifyCodeTimer.cancel();
        m_verifyOpinionTimer.cancel();

        m_drive.m_eventHandler.verificationTransactionIsReady( m_drive.m_replicator, *m_myVerificationApprovalTxInfo );
    }

    void verifyCodeTimerExpired()
    {
        DBG_MAIN_THREAD

        _LOG( "Verify Code Timer Expired " << m_request->m_tx )

        _ASSERT( !m_verificationMustBeInterrupted )

        _ASSERT( m_myVerifyCodesCalculated )
        _ASSERT( !m_verifyApproveTxSent )

        // Prepare 'Verify Approval Tx Info'
        m_myOpinion = {
            m_request->m_tx.array(),
            m_drive.m_driveKey.array(),
            m_request->m_shardId,
            {} };

        VerifyOpinion myOpinion = {m_drive.m_replicator.dbgReplicatorKey().array(), {}, {} };

        auto& keyList = m_request->m_replicators;
        myOpinion.m_opinions.resize( keyList.size() );

        for( size_t i=0; i<keyList.size(); i++ )
        {
            auto& key = keyList[i].array();

            if ( key == m_drive.m_replicator.keyPair().publicKey().array() )
            {
                myOpinion.m_opinions[i] = 1;
            }
            else
            {
                if ( auto verifyInfoIt = m_receivedCodes.find(key); verifyInfoIt != m_receivedCodes.end() )
                {
                    myOpinion.m_opinions[i] = (verifyInfoIt->second.m_code == m_verificationCodes[i]);
                }
                else
                {
                    myOpinion.m_opinions[i] = 0;
                }
            }
        }

        myOpinion.Sign( m_drive.m_replicator.keyPair(), m_myOpinion->m_tx, m_myOpinion->m_driveKey, m_myOpinion->m_shardId );

        m_myOpinion->m_opinions.push_back( myOpinion );

        shareVerifyOpinion();

        _ASSERT ( processedVerificationOpinion( {*m_myOpinion} ) );
    }


    void shareVerifyCode()
    {
        DBG_MAIN_THREAD

        _ASSERT( m_myCodeInfo )

        std::ostringstream os( std::ios::binary );
        cereal::PortableBinaryOutputArchive archive( os );
        archive( *m_myCodeInfo );

        //
        // Send message to other replicators
        //
        for( const auto& replicatorKey: m_request->m_replicators )
        {
            if ( replicatorKey != m_drive.m_replicator.dbgReplicatorKey() )
            {
                m_drive.m_replicator.sendMessage( "code_verify", replicatorKey, os.str() );
            }
        }

        if ( auto session = m_drive.m_session.lock(); session )
        {
            m_shareVerifyCodeTimer = session->startTimer( m_drive.m_replicator.getVerificationShareTimerDelay(),
                                                        [this]() {
                shareVerifyCode();
            } );
        }
    }

    void shareVerifyOpinion()
    {
        DBG_MAIN_THREAD

        _ASSERT( m_myOpinion )

        std::ostringstream os( std::ios::binary );
        cereal::PortableBinaryOutputArchive archive( os );
        archive( *m_myOpinion );

        for( const auto& replicatorKey: m_request->m_replicators )
        {
            if ( replicatorKey != m_drive.m_replicator.keyPair().publicKey() )
            {
                m_drive.m_replicator.sendMessage( "verify_opinion", replicatorKey.array(), os.str() );
            }
        }

        if ( auto session = m_drive.m_session.lock(); session )
        {
            m_shareVerifyOpinionTimer = session->startTimer( m_drive.m_replicator.getVerificationShareTimerDelay(),
                                                        [this]() {
                shareVerifyOpinion();
            } );
        }
    }
};

std::shared_ptr<DriveTaskBase> createDriveVerificationTask( mobj<VerificationRequest>&& request,
                                                            std::vector<VerifyApprovalTxInfo>&& receivedOpinions,
                                                            std::vector<VerificationCodeInfo>&& receivedCodes,
                                                            DriveParams& drive )
{
    return std::make_shared<VerificationDriveTask>( std::move(request), std::move(receivedOpinions), std::move(receivedCodes), drive );
}

}

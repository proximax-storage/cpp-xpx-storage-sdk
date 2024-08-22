/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "types.h"
#include "drive/log.h"
#include "drive/FlatDrive.h"
#include "drive/ModificationsExecutor.h"
#include "drive/Utils.h"
#include "drive/Session.h"
#include "DownloadLimiter.h"
#include "RcptSyncronizer.h"
#include "BackgroundExecutor.h"
#include "wsserver/Listener.h"
#include "utils/HexParser.h"

#include <boost/algorithm/hex.hpp>

#ifndef SKIP_GRPC
#include <drive/RPCService.h>

#include <supercontract-server/StorageServer.h>
#include <messenger-server/Messenger.h>
#include <messenger-server/MessengerServerBuilder.h>

#endif

#include <cereal/types/vector.hpp>
#include <cereal/types/map.hpp>
#include <cereal/archives/portable_binary.hpp>

#include <libtorrent/alert_types.hpp>

#include <filesystem>
#include <mutex>
#include <future>

#undef DBG_MAIN_THREAD
#define DBG_MAIN_THREAD _FUNC_ENTRY; assert( m_dbgThreadId == std::this_thread::get_id() );

namespace sirius::drive
{

//
// DefaultReplicator
//
class DefaultReplicator
        : public DownloadLimiter,
#ifndef SKIP_GRPC
          public messenger::Messenger,
#endif
          public std::enable_shared_from_this<DefaultReplicator>    // Replicator
{
private:
    boost::asio::io_context m_replicatorContext;
    std::thread m_libtorrentThread;

	std::vector<std::thread> m_wsThreads;
	boost::asio::io_context m_wsServerContext;

    // Session listen interface
    std::string m_address;
    std::string m_port;
	std::string m_wsPort;

    // Folders for drives and sandboxes
    std::string m_storageDirectory;

    int m_downloadApprovalTransactionTimerDelayMs = 10 * 1000;
    int m_modifyApprovalTransactionTimerDelayMs = 10 * 1000;
    int m_verifyCodeTimerDelayMs = 5 * 60 * 1000;
    int m_verifyApprovalTransactionTimerDelayMs = 10 * 1000;
    int m_shareMyDownloadOpinionTimerDelayMs = 60 * 1000;
    int m_verificationShareTimerDelay = 60 * 1000;
    uint64_t m_minReplicatorsNumber = 4;

    ReplicatorEventHandler&             m_eventHandler;
    DbgReplicatorEventHandler*          m_dbgEventHandler;

    std::vector<ReplicatorInfo>         m_bootstraps;
    RcptSyncronizer                     m_dnOpinionSyncronizer;

    // key is verify tx
    std::map<std::array<uint8_t, 32>, VerifyOpinion> m_verifyApprovalMap;

    BackgroundExecutor m_backgroundExecutor;

#ifndef SKIP_GRPC
    std::optional<std::string> m_serviceServerAddress;
    std::vector<std::shared_ptr<RPCService>> m_services;
    std::unique_ptr<grpc::Server> m_serviceServer;

    std::map<std::string, std::shared_ptr<messenger::MessageSubscriber>> m_messageSubscribers;
#endif

    bool m_dbgAllowCreateNonExistingDrives = false;
    
public:
    DefaultReplicator(
            const crypto::KeyPair& keyPair,
            std::string address,
            std::string port,
			std::string wsPort,
            std::string storageDirectory,
            bool useTcpSocket,
            ReplicatorEventHandler& handler,
            DbgReplicatorEventHandler* dbgEventHandler,
            const std::vector<ReplicatorInfo>& bootstraps,
            std::string  dbgReplicatorName,
            std::string  logOptions
            ) : DownloadLimiter( keyPair, dbgReplicatorName ),

        m_address( address ),
        m_port( port ),
		m_wsPort( wsPort ),
        m_storageDirectory( storageDirectory ),
        m_eventHandler( handler ),
        m_dbgEventHandler( dbgEventHandler ),
        m_bootstraps(bootstraps),
        m_dnOpinionSyncronizer( *this, m_dbgOurPeerName )
    {
        _LOG("Replicator Public Key: " << m_keyPair.publicKey())
    }

    bool isStopped() override
    {
        //DBG_MAIN_THREAD

        return m_isDestructing;
    }

    void executeOnBackgroundThread( const std::function<void()>& task ) override
    {
        DBG_MAIN_THREAD

        m_backgroundExecutor.execute( [=]
                                      { task(); } );
    }

    void stopReplicator()
    {
        DBG_MAIN_THREAD

        m_isDestructing = true;

#ifndef SKIP_GRPC
        if (m_serviceServer) {
        	m_serviceServer->Shutdown(std::chrono::system_clock::now());
        }
        m_services.clear();
#endif
        
        m_session->endSession();

        m_dnOpinionSyncronizer.stop();

        for ( auto&[key, drive]: m_driveMap )
        {
            drive->shutdown();
        }

        for ( auto&[channelId, value]: m_dnChannelMap )
        {
            for ( auto&[event, opinion]: value.m_downloadOpinionMap )
            {
                opinion.m_timer.cancel();
                opinion.m_opinionShareTimer.cancel();
            }
        }
    }

    void shutdownReplicator() override
	{
		_LOG_WARN( "shutdownReplicator START: ")

		// TODO: stop ws serer here
		for (auto& thread : m_wsThreads)
		{
			if (thread.joinable())
			{
				thread.join();
			}
		}

		_LOG_WARN( "shutdownReplicator ws server stopped")

        std::promise<void> barrier;
        boost::asio::post( m_session->lt_session().get_context(), [&barrier, this]() mutable
		{
            DBG_MAIN_THREAD
            stopReplicator();
            barrier.set_value();
        } );
        barrier.get_future().wait();

        m_backgroundExecutor.stop();

		_LOG_WARN( "shutdownReplicator replicator stopped")

		m_session->lt_session().abort();
        m_session.reset();

		_LOG_WARN( "shutdownReplicator libtorrent session stopped")

        if ( m_libtorrentThread.joinable())
		{
            _LOG( "m_libtorrentThread joined" )
            m_libtorrentThread.join();
        }

        //(???+++)
        saveDownloadChannelMap();

		_LOG_WARN( "shutdownReplicator END: ")
    }

    ~DefaultReplicator() override
    {

#ifdef DEBUG_OFF_CATAPULT
        _LOG( "~DefaultReplicator() " )
#endif
    }

    void start() override
    {
        loadDownloadChannelMap();

        m_session = createDefaultSession( m_replicatorContext, m_address + ":" + m_port,
                                          [port = m_port, this]( const lt::alert* pAlert )
                                          {
                                              if ( pAlert->type() == lt::listen_failed_alert::alert_type )
                                              {
                                                  _LOG_WARN( "Replicator session alert: " << pAlert->message());
                                                  _LOG_WARN( "Port is busy?: " << port );
                                                  m_eventHandler.onLibtorrentSessionError( pAlert->message());
                                              }
                                          },
                                          weak_from_this(),
                                          weak_from_this(),
                                          m_bootstraps);

        m_session->lt_session().m_dbgOurPeerName = m_dbgOurPeerName;

        m_libtorrentThread = std::thread( [this]
                                          {
                                              //m_sesion->setDbgThreadId();
                                              m_dbgThreadId = std::this_thread::get_id();

                                              _LOG( "libtorrentThread started: ");
                                              m_replicatorContext.run();
#ifdef DEBUG_OFF_CATAPULT
                                              _LOG( "libtorrentThread ended" );
#endif
                                          } );
        m_dbgThreadId = m_libtorrentThread.get_id();
        _LOG( "m_dbgThreadId 1234 = " << m_dbgThreadId )

        boost::asio::post( m_session->lt_session().get_context(), [=, this]() mutable
        {

            DBG_MAIN_THREAD

#ifndef SKIP_GRPC
            if ( !m_services.empty() )
            {
                SIRIUS_ASSERT(m_serviceServerAddress)
                grpc::ServerBuilder builder;
                builder.AddListeningPort( *m_serviceServerAddress, grpc::InsecureServerCredentials());
                for (const auto& service: m_services)
				{
                    service->registerService(builder);
                }

                m_serviceServer = builder.BuildAndStart();
                for (const auto& service: m_services)
				{
                    service->run(m_session);
                }
            }
#endif
        } );

        m_dnOpinionSyncronizer.start( m_session );

		auto wsServer = std::make_shared<sirius::wsserver::Listener>(m_wsServerContext, m_keyPair, m_storageDirectory);
		const auto address = boost::asio::ip::make_address(m_address);
		const auto rawPort = boost::lexical_cast<unsigned short>(m_wsPort);
		const auto wsEndpoint = boost::asio::ip::tcp::endpoint{ address, rawPort };

		auto fsTreeHandler = [this](boost::property_tree::ptree data, std::function<void(boost::property_tree::ptree fsTreeJson)> callback)
		{
			for (const auto& fsTreeInfo : data)
			{
				boost::property_tree::ptree info = fsTreeInfo.second;
				auto driveKey = info.get_optional<std::string>("driveKey");
				if (!driveKey.has_value() || driveKey.value().empty())
				{
					_LOG( "fsTreeHandler: invalid drive key!")
					// TODO: send error to client
					continue;
				}

				auto rootHash = info.get_optional<std::string>("rootHash");
				if (!rootHash.has_value() || rootHash.value().empty())
				{
					_LOG( "fsTreeHandler: invalid root hash!")
					// TODO: send error to client
					continue;
				}

				sirius::Key rawDriveKey;
				sirius::utils::ParseHexStringIntoContainer(driveKey.value().c_str(), driveKey.value().size(), rawDriveKey);

				sirius::Key rawRootHash;
				sirius::utils::ParseHexStringIntoContainer(rootHash.value().c_str(), rootHash.value().size(), rawRootHash);

				boost::asio::post( m_session->lt_session().get_context(), [this, rawDriveKey, driveKey, rootHash, rawRootHash, callback]()
				{
					DBG_MAIN_THREAD

					auto driveIt = m_driveMap.find(rawDriveKey);
					if (driveIt == m_driveMap.end())
					{
						// TODO: find on other replicators
						_LOG( "fsTreeHandler: drive not found: " << driveKey)
					}
//					else if (driveIt->second->rootHash() == rawRootHash.array())
//					{
//						// TODO: send response to client
//						_LOG( "fsTreeHandler: fs tree is up to date. Root hash: " << rootHash)
//					}
					else
					{
						boost::property_tree::ptree pTree;
						driveIt->second->getFsTreeAsJson(pTree);

						boost::asio::post( m_wsServerContext, [this, callback, pTree]()
						{
							callback(pTree);
						});
					}
				});
			}
		};

		wsServer->setFsTreeHandler(fsTreeHandler);
		wsServer->init(wsEndpoint);
		wsServer->run();
    }

    Hash256 dbgGetRootHash( const DriveKey& driveKey ) override
    {
        std::promise<Hash256> thePromise;
        auto future = thePromise.get_future();

        boost::asio::post( m_session->lt_session().get_context(), [=, &thePromise, this]()
        {
            DBG_MAIN_THREAD

            const auto drive = getDrive( driveKey );
            SIRIUS_ASSERT( drive );
            auto rootHash = drive->rootHash();
            thePromise.set_value( rootHash );
        } );

        return future.get();
    }

    void dbgPrintDriveStatus( const Key& driveKey ) override
    {
        boost::asio::post( m_session->lt_session().get_context(), [=, this]()
        {
            DBG_MAIN_THREAD

            if ( const auto drive = getDrive( driveKey ); drive )
            {
                return drive->dbgPrintDriveStatus();
            }

            _LOG_ERR( "unknown drive: " << driveKey );
            throw std::runtime_error( std::string( "unknown dive: " ) + toString( driveKey.array()));
        } );

    }

    void asyncInitializationFinished() override
    {
        _FUNC_ENTRY

		const unsigned int threadsAmount = std::thread::hardware_concurrency();
		m_wsThreads.reserve(threadsAmount);
		for (int i = 0; i < threadsAmount; ++i)
		{
			m_wsThreads.emplace_back([pThis = shared_from_this()]{ pThis->m_wsServerContext.run(); });
		}

        boost::asio::post( m_session->lt_session().get_context(), [this]
        {
            removeUnusedDrives( m_storageDirectory );
        } );
    }

    void asyncAddDrive( Key driveKey, mobj<AddDriveRequest>&& driveRequest ) override
    {
        _FUNC_ENTRY

        boost::asio::post(m_session->lt_session().get_context(), [=,driveRequest=std::move(driveRequest),this]() mutable {

            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            _LOG( "adding drive " << driveKey );

            if ( m_driveMap.find( driveKey ) != m_driveMap.end())
            {
                _LOG_ERR( "drive already added" );
                return;
            }

            // Exclude itself from replicator list
            for ( auto it = driveRequest->m_fullReplicatorList.begin();
                  it != driveRequest->m_fullReplicatorList.end(); it++ )
            {
                if ( *it == publicKey())
                {
                    driveRequest->m_fullReplicatorList.erase( it );
                    break;
                }
            }

            auto drive = sirius::drive::createDefaultFlatDrive(
                    session(),
                    m_storageDirectory,
                    driveKey,
                    driveRequest->m_client,
                    driveRequest->m_driveSize,
                    driveRequest->m_expectedCumulativeDownloadSize,
                    std::move( driveRequest->m_completedModifications ),
                    m_eventHandler,
                    *this,
                    driveRequest->m_fullReplicatorList,
                    driveRequest->m_modifyDonatorShard,
                    driveRequest->m_modifyRecipientShard,
                    m_dbgEventHandler );

            m_driveMap[driveKey] = drive;

            m_session->startSearchPeerEndpoints( driveRequest->m_fullReplicatorList );
            m_session->addClientToLocalEndpointMap( driveRequest->m_client );

            // Notify
            if ( m_dbgEventHandler )
            {
                m_dbgEventHandler->driveAdded( drive->drivePublicKey());
            }
        } );//post
    }

    void asyncRemoveDrive( Key driveKey ) override
    {
        _FUNC_ENTRY

        boost::asio::post( m_session->lt_session().get_context(), [=, this]() mutable
        {
            DBG_MAIN_THREAD

            //(???) What will happen after restart?
            if ( m_isDestructing )
            {
                return;
            }

            if ( auto drive = getDrive( driveKey ); drive )
            {
                drive->startDriveClosing(  std::make_unique<DriveClosureRequest>() );
            }
            else
            {
                _LOG_ERR( "drive not found: " << driveKey );
                return;
            }
        } );
    }

    void asyncSetReplicators( Key driveKey, mobj<ReplicatorList>&& replicatorKeys ) override
    {
        //_FUNC_ENTRY

    	boost::asio::post(m_session->lt_session().get_context(), [=,replicatorKeys=std::move(replicatorKeys),this]() mutable
        {
            DBG_MAIN_THREAD

            if ( auto drive = getDrive( driveKey ); drive )
            {
                for ( auto it = replicatorKeys->begin(); it != replicatorKeys->end(); it++ )
                {
                    if ( *it == publicKey())
                    {
                        replicatorKeys->erase( it );
                        break;
                    }
                }

                m_session->startSearchPeerEndpoints( *replicatorKeys );

                drive->setReplicators( std::move( replicatorKeys ));
            } else
            {
                _LOG_ERR( "drive not found: " << driveKey );
                return;
            }
        } );
    }

    // It notifies about changes in modification shards
    void asyncSetShardDonator( Key driveKey, mobj<ReplicatorList>&& replicatorKeys ) override
    {
        _FUNC_ENTRY

    	boost::asio::post(m_session->lt_session().get_context(), [=,replicatorKeys=std::move(replicatorKeys),this]() mutable
        {
            DBG_MAIN_THREAD

            if ( auto drive = getDrive( driveKey ); drive )
            {
                drive->setShardDonator( std::move( replicatorKeys ));
            } else
            {
                _LOG_ERR( "drive not found: " << driveKey );
                return;
            }
        } );
    }

    void asyncSetShardRecipient( Key driveKey, mobj<ReplicatorList>&& replicatorKeys ) override
    {
        _FUNC_ENTRY

    	boost::asio::post(m_session->lt_session().get_context(), [=,replicatorKeys=std::move(replicatorKeys),this]() mutable
        {
            DBG_MAIN_THREAD

            if ( auto drive = getDrive( driveKey ); drive )
            {
                drive->setShardRecipient( std::move( replicatorKeys ));
            } else
            {
                _LOG_ERR( "drive not found: " << driveKey );
                return;
            }
        } );
    }

	virtual void asyncSetChanelShard( mobj<Hash256>&& channelId, mobj<ReplicatorList>&& replicatorKeys ) override {
    	boost::asio::post(m_session->lt_session().get_context(), [channelId=std::move(channelId),replicatorKeys=std::move(replicatorKeys),this]() mutable
        {
            DBG_MAIN_THREAD

            if ( auto channelInfoIt = m_dnChannelMap.find( channelId->array()); channelInfoIt != m_dnChannelMap.end())
            {
                channelInfoIt->second.m_dnReplicatorShard = *replicatorKeys;
            } else
            {
                _LOG_ERR( "Unknown channel hash: " << *channelId );
                return;
            }
        } );
    }

    void asyncCloseDrive( Key driveKey, Hash256 transactionHash ) override
    {
        _FUNC_ENTRY

        boost::asio::post( m_session->lt_session().get_context(), [=, this]() mutable
        {

            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            if ( auto drive = getDrive( driveKey ); drive )
            {
                drive->startDriveClosing( std::make_unique<DriveClosureRequest>(transactionHash) );
            }
            else
            {
                _LOG_ERR( "removeDrive: drive not found: " << driveKey );
                return;
            }
        } );//post
    }

    void asyncModify( Key driveKey, mobj<ModificationRequest>&& modifyRequest ) override
    {
        _FUNC_ENTRY

        _LOG( "+++ ex startModifyDrive: " << modifyRequest->m_clientDataInfoHash )
        _LOG("m_uploadedDataSize: +++ ex startModifyDrive: " << modifyRequest->m_maxDataSize )

        boost::asio::post(m_session->lt_session().get_context(), [=,modifyRequest=std::move(modifyRequest),this]() mutable {

            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            std::shared_ptr<sirius::drive::FlatDrive> pDrive;
            {
                if ( auto drive = getDrive( driveKey ); drive )
                {
                    pDrive = drive;
                } else
                {
                    _LOG( "asyncModify(): drive not found: " << driveKey );
                    return;
                }
            }

            for ( auto it = modifyRequest->m_replicatorList.begin(); it != modifyRequest->m_replicatorList.end(); it++ )
            {
                if ( *it == publicKey())
                {
                    modifyRequest->m_replicatorList.erase( it );
                    break;
                }
            }

            pDrive->startModifyDrive( std::move( modifyRequest ));
        } );//post
    }

    void asyncCancelModify( Key driveKey, Hash256 transactionHash ) override
    {
        _FUNC_ENTRY

        boost::asio::post( m_session->lt_session().get_context(), [=, this]() mutable
        {

            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            if ( const auto drive = getDrive( driveKey ); drive )
            {
                drive->cancelModifyDrive( std::make_unique<ModificationCancelRequest>(transactionHash) );
                return;
            }

            _LOG( "asyncCancelModify: unknown drive: " << driveKey );
        } );//post
    }

    void asyncStartDriveVerification( Key driveKey, mobj<VerificationRequest>&& request ) override
    {
        _FUNC_ENTRY

        boost::asio::post(m_session->lt_session().get_context(), [=,request=std::move(request),this]() mutable {

            DBG_MAIN_THREAD

            _LOG( "started verification" );

            if ( m_isDestructing )
            {
                return;
            }

            if ( const auto drive = getDrive( driveKey ); drive )
            {
                drive->startVerification( std::move( request ));
                return;
            }

            _LOG( "asyncStartDriveVerification: unknown drive: " << driveKey );
        } );//post
    }

    void asyncCancelDriveVerification( Key driveKey ) override
    {
        _FUNC_ENTRY

        //TODO
        boost::asio::post( m_session->lt_session().get_context(), [=, this]() mutable
        {

            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            if ( const auto drive = getDrive( driveKey ); drive )
            {
                drive->cancelVerification();
                return;
            }

            _LOG( "asyncCancelDriveVerification: unknown drive: " << driveKey );
        } );//post
    }

    void asyncStartStream( Key driveKey, mobj<StreamRequest>&& request ) override
    {
        _FUNC_ENTRY

        boost::asio::post(m_session->lt_session().get_context(), [=,request=std::move(request),this]() mutable {

            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            if ( auto drive = getDrive( driveKey ); drive )
            {
                drive->startStream( std::move( request ));
            } else
            {
                _LOG( "asyncModify(): drive not found: " << driveKey );
            }
        } );//post
    }

    void asyncIncreaseStream( Key driveKey, mobj<StreamIncreaseRequest>&& ) override
    {
        _FUNC_ENTRY

        boost::asio::post( m_session->lt_session().get_context(), [=, this]() mutable
        {

            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            if ( const auto drive = getDrive( driveKey ); drive )
            {
                //drive->increaseStream( std::move(request) );
                return;
            }

            _LOG( "unknown drive: " << driveKey );
        } );//post
    }

    void asyncFinishStreamTxPublished( Key driveKey, mobj<StreamFinishRequest>&& finishInfo ) override
    {
        _FUNC_ENTRY

        boost::asio::post(m_session->lt_session().get_context(), [=,finishInfo=std::move(finishInfo),this]() mutable {

            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            if ( auto driveIt = m_driveMap.find( driveKey ); driveIt != m_driveMap.end())
            {
                driveIt->second->acceptFinishStreamTx( std::move( finishInfo ));
            } else
            {
                _LOG_WARN( "Unknown drive: " << Key( driveKey ))
            }

        } );//post
    }

    void asyncAddDownloadChannelInfo( Key driveKey, mobj<DownloadRequest>&& request, bool mustBeSyncronized ) override
    {
        _FUNC_ENTRY

        boost::asio::post(m_session->lt_session().get_context(), [=,request=std::move(request),this]() mutable {

            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            std::vector<std::array<uint8_t, 32>> clientList;
            for ( const auto& it : request->m_clients )
                clientList.push_back( it.array());

            addChannelInfo( request->m_channelKey.array(),
                            request->m_prepaidDownloadSize,
                            driveKey,
                            request->m_replicators,
                            clientList,
                            mustBeSyncronized );

            if ( mustBeSyncronized )
            {
                if ( std::shared_ptr<sirius::drive::FlatDrive> drive = getDrive( driveKey ); drive )
                {
                    auto replicators = drive->getAllReplicators();
                    size_t consensusThreshould = std::min((replicators.size() * 3) / 2, size_t( 4 ));

                    m_dnOpinionSyncronizer.startSync( request->m_channelKey.array(), drive, consensusThreshould );
                } else
                {
                    _LOG_WARN( "unknown drive:" << driveKey );
                }
            }
        } );//post
    }

    virtual void asyncIncreaseDownloadChannelSize( ChannelId channelId, uint64_t size ) override
    {
        boost::asio::post( m_session->lt_session().get_context(), [=, this]() mutable
        {
            DBG_MAIN_THREAD

            increaseChannelSize( channelId.array(), size );
        } );//post
    }

    virtual DownloadChannelInfo* getDownloadChannelInfo( const std::array<uint8_t, 32>& driveKey,
                                                         const std::array<uint8_t, 32>& downloadChannelHash ) override
    {
        DBG_MAIN_THREAD

        if ( auto infoIt = m_dnChannelMap.find( downloadChannelHash ); infoIt != m_dnChannelMap.end())
        {
            if ( infoIt->second.m_driveKey != driveKey )
            {
                _LOG_ERR( "Invalid driveKey: " << Key( driveKey ) << " vs: " << Key( infoIt->second.m_driveKey ));
                return nullptr;
            }
            return &infoIt->second;
        }

        return nullptr;
    }


    void asyncRemoveDownloadChannelInfo( ChannelId channelId ) override
    {
        _FUNC_ENTRY

        boost::asio::post( m_session->lt_session().get_context(), [=, this]() mutable
        {

            DBG_MAIN_THREAD

            removeChannelInfo( channelId );
        } );
    }

    // It sends received receipt from 'client' to other replicators
    virtual void sendReceiptToOtherReplicators( const std::array<uint8_t, 32>& downloadChannelId,
                                                const std::array<uint8_t, 32>& clientPublicKey,
                                                uint64_t downloadedSize,
                                                const std::array<uint8_t, 64>& signature ) override
    {
        DBG_MAIN_THREAD

        auto replicatorPublicKey = publicKey();

//        std::vector<uint8_t> message;
//        message.insert( message.end(), downloadChannelId.begin(),   downloadChannelId.end() );
//        message.insert( message.end(), clientPublicKey.begin(),     clientPublicKey.end() );
//        message.insert( message.end(), replicatorPublicKey.begin(), replicatorPublicKey.end() );
//        message.insert( message.end(), (uint8_t*)&downloadedSize,   ((uint8_t*)&downloadedSize)+8 );
//        message.insert( message.end(), signature.begin(),           signature.end() );

        RcptMessage msg( downloadChannelId, clientPublicKey, replicatorPublicKey, downloadedSize, signature );

        if ( auto it = m_dnChannelMap.find( downloadChannelId ); it != m_dnChannelMap.end())
        {
            // go throw replictor list
            for ( auto replicatorIt = it->second.m_dnReplicatorShard.begin();
                  replicatorIt != it->second.m_dnReplicatorShard.end(); replicatorIt++ )
            {
                if ( *replicatorIt != replicatorPublicKey )
                {
                    sendMessage( "rcpt", replicatorIt->array(), msg );
                }
            }
        }
    }

    // 
    void onEndpointDiscovered( const std::array<uint8_t, 32>& key,
                               const std::optional<boost::asio::ip::udp::endpoint>& endpoint ) override
    {
        //DBG_MAIN_THREAD

        _LOG ( "onEndpointDiscovered. public key: " << toString(key) << " endpoint: " << endpoint.value().address().to_string() << " : " << endpoint.value().port())
        m_session->onEndpointDiscovered( key, endpoint );
    }

    std::optional<boost::asio::ip::udp::endpoint> getEndpoint( const std::array<uint8_t, 32>& key ) override
    {
        return m_session->getEndpoint( key );
    }

    virtual void asyncOnDownloadOpinionReceived( mobj<DownloadApprovalTransactionInfo>&& anOpinion ) override
    {
        _FUNC_ENTRY

        boost::asio::post(m_session->lt_session().get_context(), [=,anOpinion=std::move(anOpinion),this]() mutable {

            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            if ( anOpinion->m_opinions.size() != 1 )
            {
                _LOG_ERR( "onDownloadOpinionReceived: invalid opinion format: anOpinion.m_opinions.size() != 1" )
                return;
            }

            addOpinion( std::move( anOpinion ));
        } );
    }

    void processDownloadOpinion( const DownloadApprovalTransactionInfo& anOpinion ) override
    {
        DBG_MAIN_THREAD

        m_eventHandler.downloadOpinionHasBeenReceived( *this, anOpinion );
    }

    DownloadOpinion createMyOpinion( const DownloadChannelInfo& info )
    {
        DBG_MAIN_THREAD

        DownloadOpinion myOpinion( publicKey());

        for ( const auto& replicatorIt : info.m_dnReplicatorShard )
        {
            if ( auto downloadedIt = info.m_replicatorUploadRequestMap.find( replicatorIt.array());
                 downloadedIt != info.m_replicatorUploadRequestMap.end() )
            {
                myOpinion.m_downloadLayout.push_back( {downloadedIt->first.array(), downloadedIt->second.totalAcceptedReceiptSize() } );
            }
            else
            {
                myOpinion.m_downloadLayout.push_back( {replicatorIt.array(), 0} );
            }
        }

        return myOpinion;
    }

    bool createSyncRcpts( const DriveKey& driveKey,
                          const ChannelId& channelId,
                          std::ostringstream& outOs,
                          Signature& outSignature ) override
    {
        DBG_MAIN_THREAD

        if ( auto drive = getDrive( driveKey ); drive )
        {
            if ( auto channelInfoIt = m_dnChannelMap.find( channelId ); channelInfoIt != m_dnChannelMap.end())
            {
                cereal::PortableBinaryOutputArchive archive( outOs );
                ReplicatorKey replicatorKey = m_keyPair.publicKey();
                archive( replicatorKey );
                archive( channelId );

                // parse and accept receipts
                //
                for ( auto&[clientKey, clientRcptMap] : channelInfoIt->second.m_clientReceiptMap )
                {
                    for ( auto&[key, msg] : clientRcptMap )
                    {
                        archive( msg );
                    }
                }

                auto str = outOs.str();
                crypto::Sign( m_keyPair, {utils::RawBuffer{(const uint8_t*) str.c_str(), str.size()}}, outSignature );
            }

            return true;
        }

        _LOG_WARN( "drive not found" );
        cereal::PortableBinaryOutputArchive archive( outOs );
        ReplicatorKey replicatorKey = m_keyPair.publicKey();
        archive( replicatorKey );
        archive( channelId );
        return false;
    }

    bool createChannelStatus( const DriveKey&       driveKey,
                              const ChannelId&      channelId,
                              std::ostringstream&   outOs,
                              Signature&            outSignature )
    {
        DBG_MAIN_THREAD

        if ( auto channelInfoIt = m_dnChannelMap.find(channelId); channelInfoIt != m_dnChannelMap.end() )
        {
            cereal::PortableBinaryOutputArchive archive( outOs );
            ReplicatorKey replicatorKey = m_keyPair.publicKey();
            archive( replicatorKey );
            archive( channelId );
            archive( channelInfoIt->second );

            auto str = outOs.str();
            crypto::Sign( m_keyPair, { utils::RawBuffer{ (const uint8_t*)str.c_str(), str.size() } }, outSignature);
            return true;
        }

        _LOG_WARN( "channel not found" );
        cereal::PortableBinaryOutputArchive archive( outOs );
        ReplicatorKey replicatorKey = m_keyPair.publicKey();
        archive( replicatorKey );
        archive( channelId );
        return false;
    }

    bool createModificationStatus( const DriveKey&       driveKey,
                                   const Hash256&        modificationHash,
                                   std::ostringstream&   outOs,
                                   Signature&            outSignature,
                                   bool&                 outIsModificationFinished )
    {
        DBG_MAIN_THREAD

        cereal::PortableBinaryOutputArchive archive( outOs );
        ReplicatorKey replicatorKey = m_keyPair.publicKey();
        archive( replicatorKey );
        archive( modificationHash );

        bool isFound = false;
        outIsModificationFinished = false;

        if ( auto driveIt = m_driveMap.find(driveKey.array()); driveIt != m_driveMap.end() )
        {
            auto* info = driveIt->second->findModifyInfo( modificationHash, outIsModificationFinished );
            if ( info != nullptr )
            {
                isFound = true;
                archive( *info );
            }
        }

        auto str = outOs.str();
        crypto::Sign( m_keyPair, { utils::RawBuffer{ (const uint8_t*)str.c_str(), str.size() } }, outSignature);
        return isFound;
    }

    void addOpinion( mobj<DownloadApprovalTransactionInfo>&& opinion )
    {
        DBG_MAIN_THREAD

        //
        // remove outdated entries (by m_creationTime)
        //
        auto now = boost::posix_time::microsec_clock::universal_time();

        for ( auto &[downloadChannelId, downloadChannel]: m_dnChannelMap )
        {
            //TODO Potential performance bottleneck
            std::erase_if( downloadChannel.m_downloadOpinionMap, [&now]( const auto& item )
            {
                const auto&[key, value] = item;
                return (now - value.m_creationTime).total_seconds() > 60 * 60;
            } );
        }

        //
        // add opinion
        //
        auto channelIt = m_dnChannelMap.find( opinion->m_downloadChannelId );

        if ( channelIt == m_dnChannelMap.end())
        {
            _LOG_WARN( "Attempt to add opinion for a non-existing channel" );
            return;
        }

        auto& channel = channelIt->second;
        auto blockHash = opinion->m_blockHash;

        if ( channel.m_downloadOpinionMap.find( opinion->m_blockHash ) == channel.m_downloadOpinionMap.end())
        {
            channel.m_downloadOpinionMap.emplace( std::make_pair( blockHash, DownloadOpinionMapValue
                    (
                            opinion->m_blockHash,
                            opinion->m_downloadChannelId,
                            {}
                    )));
        }

        auto& opinionInfo = channel.m_downloadOpinionMap[blockHash];
        auto& opinions = opinionInfo.m_opinions;
        opinions[opinion->m_opinions[0].m_replicatorKey] = opinion->m_opinions[0];

        // check opinion number
        //_LOG( "///// " << opinionInfo.m_opinions.size() << " " <<  (opinionInfo.m_replicatorNumber*2)/3 );
#ifndef MINI_SIGNATURE
        auto replicatorNumber = (std::max((std::size_t)getMinReplicatorsNumber(), channel.m_dnReplicatorShard.size()) * 2) / 3 + 1;
#else
        auto replicatorNumber = (channel.m_dnReplicatorShard.size() * 2) / 3;
#endif
        if ( opinions.size() >= replicatorNumber )
        {
            // start timer if it is not started
            if ( !opinionInfo.m_timer )
            {
                //todo check
                opinionInfo.m_timer = m_session->startTimer( m_downloadApprovalTransactionTimerDelayMs,
                                                             [this, channelId = opinion->m_downloadChannelId, blockHash = blockHash]()
                                                             {
                                                                 onDownloadApprovalTimeExpired(
                                                                         ChannelId( channelId ), Hash256( blockHash ));
                                                             } );
            }
        }
    }

    void onDownloadApprovalTimeExpired( const ChannelId& channelId, const Hash256& blockHash )
    {
        DBG_MAIN_THREAD

        auto channelIt = m_dnChannelMap.find( channelId );

        SIRIUS_ASSERT( channelIt != m_dnChannelMap.end() )

        auto& downloadMapValue = channelIt->second.m_downloadOpinionMap.find( blockHash.array())->second;

        if ( downloadMapValue.m_modifyApproveTransactionSent || downloadMapValue.m_approveTransactionReceived )
        {
            return;
        }

        // notify
        std::vector<DownloadOpinion> opinions;
        for ( const auto&[replicatorId, opinion]: downloadMapValue.m_opinions )
        {
            opinions.push_back( opinion );
        }
        auto transactionInfo = DownloadApprovalTransactionInfo{downloadMapValue.m_eventHash,
                                                               downloadMapValue.m_downloadChannelId,
                                                               std::move( opinions )};
        m_eventHandler.downloadApprovalTransactionIsReady( *this, transactionInfo );
        downloadMapValue.m_modifyApproveTransactionSent = true;
    }

    virtual void asyncInitiateDownloadApprovalTransactionInfo( Hash256 blockHash, Hash256 channelId ) override
    {
        //todo make queue for several simultaneous requests of the same channelId

        _FUNC_ENTRY

        boost::asio::post( m_session->lt_session().get_context(), [=, this]() mutable
        {

            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            doInitiateDownloadApprovalTransactionInfo( blockHash, channelId );
        } );//post
    }

    void doInitiateDownloadApprovalTransactionInfo( const Hash256& blockHash, const Hash256& channelId )
    {
        DBG_MAIN_THREAD

        //todo make queue for several simultaneous requests of the same channelId

        if ( auto it = m_dnChannelMap.find( channelId.array()); it != m_dnChannelMap.end())
        {
            //
            // Create my opinion
            //

            auto myOpinion = createMyOpinion( it->second );

            myOpinion.Sign( keyPair(), blockHash.array(), channelId.array());

            auto transactionInfo = std::make_unique<DownloadApprovalTransactionInfo>( blockHash.array(),
                                                                                      channelId.array(),
                                                                                     std::vector<DownloadOpinion>{ myOpinion } );

            addOpinion( std::move( transactionInfo ));
            shareDownloadOpinion( channelId, blockHash );
        } else
        {
            _LOG_ERR( "channelId not found" );
        }
    }

    void shareDownloadOpinion( const Hash256& downloadChannel, const Hash256& eventHash )
    {
        DBG_MAIN_THREAD

        auto it = m_dnChannelMap.find( downloadChannel.array());
        SIRIUS_ASSERT( it != m_dnChannelMap.end() );

        auto eventIt = it->second.m_downloadOpinionMap.find( eventHash.array());
        SIRIUS_ASSERT( eventIt !=  it->second.m_downloadOpinionMap.end() )

        auto myOpinion = eventIt->second.m_opinions.find( publicKey());
        SIRIUS_ASSERT( myOpinion != eventIt->second.m_opinions.end() );

        DownloadApprovalTransactionInfo opinionToShare = {eventHash.array(), downloadChannel.array(),
                                                          {myOpinion->second}};

        // send opinion to other Replicators
        std::ostringstream os( std::ios::binary );
        cereal::PortableBinaryOutputArchive archive( os );
        archive( opinionToShare );

        for ( const auto& replicatorIt : it->second.m_dnReplicatorShard )
        {
            if ( replicatorIt != publicKey())
            {
                //_LOG( "replicatorIt.m_endpoint: " << replicatorIt.m_endpoint << " " << os.str().length() << " " << dbgReplicatorName() );
                sendMessage( "dn_opinion", replicatorIt.array(), os.str());
            }
        }

        // Repeat opinion sharing
        eventIt->second.m_opinionShareTimer = m_session->startTimer( m_shareMyDownloadOpinionTimerDelayMs, [=, this]
        {
            shareDownloadOpinion( downloadChannel, eventHash );
        } );
    };

    // It is called when drive is closing
    void closeDriveChannels( const mobj<Hash256>& blockHash, const Key& driveKey ) override
    {
        DBG_MAIN_THREAD

        SIRIUS_ASSERT( blockHash )
        for ( auto&[channelId, channelInfo] : m_dnChannelMap )
        {
            if ( channelInfo.m_driveKey == driveKey.array())
            {
                doInitiateDownloadApprovalTransactionInfo( *blockHash, channelId );
            }
        }
    }

    void asyncDownloadApprovalTransactionHasFailedInvalidOpinions( Hash256 eventHash, Hash256 channelId ) override
    {
        _FUNC_ENTRY

        boost::asio::post( m_session->lt_session().get_context(), [=, this]() mutable
        {

            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            if ( auto channelIt = m_dnChannelMap.find( channelId.array()); channelIt != m_dnChannelMap.end())
            {
                if ( channelIt->second.m_isClosed )
                {
                    return;
                }

                auto& opinions = channelIt->second.m_downloadOpinionMap;
                if ( auto opinionInfoIt = opinions.find( eventHash.array()); opinionInfoIt != opinions.end())
                {
                    auto& opinionInfo = opinionInfoIt->second;
                    if ( opinionInfo.m_approveTransactionReceived )
                    {
                        return;
                    }
                    if ( opinionInfo.m_timer )
                    {
                        opinionInfo.m_timer.cancel();
                    }
                    auto receivedOpinions = opinionInfo.m_opinions;
                    opinionInfo.m_opinions.clear();
                    opinionInfo.m_modifyApproveTransactionSent = false;
                    for ( const auto&[key, opinion]: receivedOpinions )
                    {
                        processDownloadOpinion( DownloadApprovalTransactionInfo
                                                        {
                                                                opinionInfo.m_eventHash,
                                                                opinionInfo.m_downloadChannelId,
                                                                {opinion}
                                                        } );
                    }
                } else
                {
                    _LOG_ERR( "eventHash not found" );
                }
            } else
            {
                _LOG_ERR( "channelId not found" );
            }
        } );//post
    }

    virtual void asyncDownloadApprovalTransactionHasBeenPublished( Hash256 eventHash, Hash256 channelId,
                                                                   bool channelMustBeClosed ) override
    {
        _FUNC_ENTRY

        boost::asio::post( m_session->lt_session().get_context(), [=, this]() mutable
        {

            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            // clear opinion map
            if ( auto channelIt = m_dnChannelMap.find( channelId.array()); channelIt != m_dnChannelMap.end())
            {
                auto& opinions = channelIt->second.m_downloadOpinionMap;
                if ( channelMustBeClosed )
                {
                    m_dnChannelMap.erase( channelIt );
                    return;
                } else if ( auto it = opinions.find( eventHash.array()); it != opinions.end())
                {
                    // TODO maybe remove the entry?
                    it->second.m_timer.cancel();
                    it->second.m_opinionShareTimer.cancel();
                    it->second.m_approveTransactionReceived = true;
                }
            } else
            {
                _LOG_ERR( "channelId not found" );
            }
        } );//post
    }

    void finishDriveClosure( const Key& driveKey ) override
    {
        DBG_MAIN_THREAD

        auto it = m_driveMap.find( driveKey );

        SIRIUS_ASSERT( it != m_driveMap.end() )

        m_driveMap.erase( it );
    }

    virtual void asyncOnOpinionReceived( ApprovalTransactionInfo anOpinion ) override
    {
        _FUNC_ENTRY

        boost::asio::post( m_session->lt_session().get_context(), [=, this]() mutable
        {

            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            if ( auto drive = getDrive( anOpinion.m_driveKey ); drive )
            {
                drive->onOpinionReceived( std::make_unique<ApprovalTransactionInfo>(anOpinion) );
            }
            else
            {
                _LOG_ERR( "drive not found" );
            }
        } );
    }


    void processOpinion( const ApprovalTransactionInfo& anOpinion ) override
    {
        DBG_MAIN_THREAD

        m_eventHandler.opinionHasBeenReceived( *this, anOpinion );
    }

    virtual void asyncApprovalTransactionHasBeenPublished(
            mobj<PublishedModificationApprovalTransactionInfo>&& transaction ) override
    {
        _FUNC_ENTRY

        _LOG( "asyncApprovalTransactionHasBeenPublished, m_rootHash:" << Key( transaction->m_rootHash ))

        boost::asio::post(m_session->lt_session().get_context(), [transaction=std::move(transaction),this]() mutable {

            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            if ( auto drive = getDrive( transaction->m_driveKey ); drive )
            {
                drive->onApprovalTransactionHasBeenPublished( *transaction );
            } else
            {
                _LOG_ERR( "drive not found" );
            }
        } );//post
    }

    void asyncApprovalTransactionHasFailedInvalidOpinions( Key driveKey, Hash256 transactionHash ) override
    {
        _FUNC_ENTRY

        boost::asio::post( m_session->lt_session().get_context(), [=, this]() mutable
        {

            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            if ( auto drive = getDrive( driveKey ); drive )
            {
                drive->onApprovalTransactionHasFailedInvalidOpinions( transactionHash );
            } else
            {
                _LOG_ERR( "drive not found" );
            }
        } );//post
    }

    virtual void asyncSingleApprovalTransactionHasBeenPublished(
            mobj<PublishedModificationSingleApprovalTransactionInfo>&& transaction ) override
    {
        _FUNC_ENTRY

        boost::asio::post(m_session->lt_session().get_context(), [transaction=std::move(transaction),this]() mutable {

            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            if ( auto drive = getDrive( transaction->m_driveKey ); drive )
            {
                drive->onSingleApprovalTransactionHasBeenPublished( *transaction );
            } else
            {
                _LOG_ERR( "drive not found" );
            }
        } );//post
    }

    virtual void
    asyncVerifyApprovalTransactionHasBeenPublished( PublishedVerificationApprovalTransactionInfo info ) override
    {
        _FUNC_ENTRY

        boost::asio::post( m_session->lt_session().get_context(), [=, this]() mutable
        {

            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            if ( auto drive = getDrive( info.m_driveKey ); drive )
            {
                drive->onVerifyApprovalTransactionHasBeenPublished( info );
            } else
            {
                _LOG_ERR( "drive not found" );
            }
        } );//post
    }

    void asyncVerifyApprovalTransactionHasFailedInvalidOpinions( Key driveKey, Hash256 verificationId ) override
    {}

    virtual void sendMessage( const std::string& query,
                              const ReplicatorKey& replicatorKey,
                              const std::string& message ) override
    {
        DBG_MAIN_THREAD

        if ( m_isDestructing )
        {
            return;
        }

        auto endpointTo = getEndpoint( replicatorKey.array() );
        if ( endpointTo )
        {
            m_session->sendMessage( query, {endpointTo->address(), endpointTo->port()}, message );
            _LOG( "sendMessage '" << query << "' to " << Key(replicatorKey) << " at " << endpointTo->address() << ":" << std::dec << endpointTo->port());
        }
        else
        {
            _LOG( "WARN!!! Failed to send '" << query << "' to " << int( replicatorKey[0] ));
        }
    }

    virtual void sendMessage( const std::string& query,
                              const ReplicatorKey& replicatorKey,
                              const std::vector<uint8_t>& message ) override
    {
        DBG_MAIN_THREAD

        if ( m_isDestructing )
        {
            return;
        }

        auto endpointTo = getEndpoint( replicatorKey.array() );
        if ( endpointTo )
        {
            //__LOG( "*** sendMessage: " << query << " to: " << *endpointTo << " " << int(replicatorKey[0]) );
            m_session->sendMessage( query, {endpointTo->address(), endpointTo->port()}, message );
        } else
        {
            __LOG( "sendMessage: absent endpoint: " << int( replicatorKey[0] ));
        }
    }

    virtual void sendSignedMessage( const std::string& query,
                                    const ReplicatorKey& replicatorKey,
                                    const std::vector<uint8_t>& message ) override
    {
        DBG_MAIN_THREAD

        if ( m_isDestructing )
        {
            return;
        }

        auto endpointTo = getEndpoint( replicatorKey.array() );
        if ( endpointTo )
        {
            Signature signature;
            crypto::Sign( m_keyPair, {utils::RawBuffer{message}}, signature );

            m_session->sendMessage( query, {endpointTo->address(), endpointTo->port()}, message, &signature );
        } else
        {
            __LOG( "sendMessage: absent endpoint: " << int( replicatorKey[0] ));
        }
    }

    virtual void onMessageReceived( const std::string& query,
                                    const std::string& message,
                                    const boost::asio::ip::udp::endpoint& source ) override
    {
        try
        {

            DBG_MAIN_THREAD

            _LOG( "query: " << query << " from: " << source.address() << ":" << source.port() );

            if ( query == "opinion" )
            {
                try
                {
                    std::istringstream is( message, std::ios::binary );
                    cereal::PortableBinaryInputArchive iarchive( is );
                    ApprovalTransactionInfo info;
                    iarchive( info );

                    processOpinion( info );
                }
                catch ( ... )
                {}

                return;
            } else if ( query == "dn_opinion" )
            {
                try
                {
                    std::istringstream is( message, std::ios::binary );
                    cereal::PortableBinaryInputArchive iarchive( is );
                    DownloadApprovalTransactionInfo info;
                    iarchive( info );
                    processDownloadOpinion( info );
                }
                catch ( ... )
                {_LOG_WARN( "execption occured" )}

                return;
            } else if ( query == "code_verify" )
            {
                try
                {
                    std::istringstream is( message, std::ios::binary );
                    cereal::PortableBinaryInputArchive iarchive( is );

                auto info = std::make_unique<VerificationCodeInfo>();
                    iarchive( *info );
                    processVerificationCode( std::move( info ));
                }
                catch ( ... )
                {_LOG_WARN( "execption occured" )}

                return;
            } else if ( query == "verify_opinion" )
            {
                try
                {
                    std::istringstream is( message, std::ios::binary );
                    cereal::PortableBinaryInputArchive iarchive( is );

                auto info = std::make_unique<VerifyApprovalTxInfo>();
                    iarchive( *info );
                    processVerificationOpinion( std::move( info ));
                }
                catch ( ... )
                {_LOG_WARN( "execption occured" )}

                return;
            }
            else if ( query == "chunk-info" )
            {
                try
                {
                    std::istringstream is( message, std::ios::binary );
                    cereal::PortableBinaryInputArchive iarchive( is );
                    std::array<uint8_t, 32> driveKey;
                    iarchive( driveKey );

                    if ( auto driveIt = m_driveMap.find( driveKey ); driveIt != m_driveMap.end())
                    {
                        ChunkInfo chunkInfo;
                        iarchive( chunkInfo );

                        driveIt->second->acceptChunkInfoMessage( chunkInfo, source );
                    }
                    else
                    {
                        _LOG_WARN( "Unknown drive: " << Key( driveKey ))
                    }
                }
                catch ( ... )
                {_LOG_WARN( "execption occured" )}

                return;
            }
#ifndef SKIP_GRPC
            else if (m_messageSubscribers.contains(query)) {
                auto it = m_messageSubscribers.find(query);

                bool enqueued = it->second->onMessageReceived(messenger::InputMessage{query, message});

                if (!enqueued) {
                    m_messageSubscribers.erase(it);
                }

				return;
            }
#endif
            
//        else if ( query == "finish-stream" )
//        {
//            try
//            {
//                std::istringstream is( message, std::ios::binary );
//                cereal::PortableBinaryInputArchive iarchive(is);
//                std::array<uint8_t,32> driveKey;
//                iarchive( driveKey );
//
//                if ( auto driveIt = m_driveMap.find( driveKey ); driveIt != m_driveMap.end() )
//                {
//                    mobj<FinishStreamMsg> finishStream{FinishStreamMsg{}};
//                    iarchive( *finishStream );
//                    SIRIUS_ASSERT( finishStream )
//
//                    driveIt->second->acceptFinishStreamTx( std::move(finishStream), source );
//                }
//                else
//                {
//                    _LOG_WARN( "Unknown drive: " << Key(driveKey) )
//                }
//            }
//            catch(...){
//                _LOG_WARN( "bad 'finish-stream' message" )
//            }
//
//            return;
//        }

            _LOG_WARN( "Unknown query: " << query );

        } catch ( ... )
        {
            _LOG_ERR( "onMessageReceived: invalid message format: query=" << query );
        }
    }

    void onSyncRcptReceived( const lt::string_view& response, const lt::string_view& sign ) override
    {
        try
        {
            std::istringstream is( std::string( response.begin(), response.end()), std::ios::binary );
            cereal::PortableBinaryInputArchive iarchive( is );

            ReplicatorKey otherReplicatorKey;
            ChannelId channelId;
            iarchive( otherReplicatorKey );
            iarchive( channelId );

            // Verify sign
            //
            Signature signature;
            if ( sign.size() != signature.size())
            {
                __LOG_WARN( "invalid sign size" )
                return;
            }
            memcpy( signature.data(), sign.data(), signature.size());

            if ( !crypto::Verify( otherReplicatorKey,
                                  {utils::RawBuffer{(const uint8_t*) response.begin(), response.size()}}, signature ))
            {
                _LOG_WARN( "invalid sign" )
                return;
            }

            m_dnOpinionSyncronizer.accpeptOpinion( channelId, otherReplicatorKey );

            // parse and accept receipts
            //
            for ( ;; )
            {
                RcptMessage msg;
                try
                {
                    iarchive( msg );
                } catch ( ... )
                {
                    return;
                }

                if ( !msg.isValidSize())
                {
                    _LOG_WARN( "invalid rcpt size" )
                    return;
                }

                acceptReceiptFromAnotherReplicator( msg );
            }
        }
        catch ( ... )
        {
            _LOG_WARN( "invalid 'get_dn_rcpts' response" )
            return;
        }
    }

    void processVerificationCode( mobj<VerificationCodeInfo>&& info )
    {
        DBG_MAIN_THREAD

        if ( !info->Verify())
        {
            _LOG_WARN( "processVerificationCode: bad sign: " << Hash256( info->m_tx ))
            return;
        }

        if ( auto driveIt = m_driveMap.find( info->m_driveKey ); driveIt != m_driveMap.end())
        {
            driveIt->second->onVerificationCodeReceived( std::move( info ));
            return;
        }

        _LOG_WARN( "processVerificationCode: unknown drive: " << Key( info->m_driveKey ));
    }

    void processVerificationOpinion( mobj<VerifyApprovalTxInfo>&& info )
    {
        DBG_MAIN_THREAD

        if ( info->m_opinions.size() != 1 )
        {
            _LOG_WARN( "processVerificationOpinion: invalid opinion size: " << info->m_opinions.size())
            return;
        }

        if ( !info->m_opinions[0].Verify( info->m_tx, info->m_driveKey, info->m_shardId ))
        {
            _LOG_WARN( "processVerificationOpinion: bad sign: " << Key( info->m_opinions[0].m_publicKey ))
            return;
        }

        if ( auto driveIt = m_driveMap.find( info->m_driveKey ); driveIt != m_driveMap.end())
        {
            driveIt->second->onVerificationOpinionReceived( std::move( info ));
            return;
        }

        _LOG_WARN( "processVerificationCode: unknown drive: " << Key( info->m_driveKey ));
    }

    void setDownloadApprovalTransactionTimerDelay( int miliseconds ) override
    {
        m_downloadApprovalTransactionTimerDelayMs = miliseconds;
    }

    void setModifyApprovalTransactionTimerDelay( int miliseconds ) override
    {
        m_modifyApprovalTransactionTimerDelayMs = miliseconds;
    }

    int getModifyApprovalTransactionTimerDelay() override
    {
        return m_modifyApprovalTransactionTimerDelayMs;
    }

    void setVerifyCodeTimerDelay( int miliseconds ) override
    {
        m_verifyCodeTimerDelayMs = miliseconds;
    }

    int getVerifyCodeTimerDelay() override
    {
        return m_verifyCodeTimerDelayMs;
    }

    void setVerifyApprovalTransactionTimerDelay( int milliseconds ) override
    {
        m_verifyApprovalTransactionTimerDelayMs = milliseconds;
    }

    int getVerifyApprovalTransactionTimerDelay() override
    {
        return m_verifyApprovalTransactionTimerDelayMs;
    }

    void setVerificationShareTimerDelay( int milliseconds ) override
    {
        m_verificationShareTimerDelay = milliseconds;
    }

    int getVerificationShareTimerDelay() override
    {
        return m_verificationShareTimerDelay;
    }

    void setMinReplicatorsNumber( uint64_t number ) override
    {
        m_minReplicatorsNumber = number;
    }

    uint64_t getMinReplicatorsNumber() override
    {
        return m_minReplicatorsNumber;
    }

    void setSessionSettings( const lt::settings_pack& settings, bool localNodes ) override
    {
        m_session->lt_session().apply_settings( settings );
        if ( localNodes )
        {
            std::uint32_t const mask = 1 << lt::session::global_peer_class_id;
            lt::ip_filter f;
            f.add_rule( lt::make_address( "0.0.0.0" ), lt::make_address( "255.255.255.255" ), mask );
            m_session->lt_session().set_peer_class_filter( f );
        }
    }

    std::string dbgReplicatorName() const override
    { return m_dbgOurPeerName.c_str(); }

//    virtual std::shared_ptr<sirius::drive::FlatDrive> dbgGetDrive( const std::array<uint8_t,32>& driveKey ) override
//    {
//        if ( auto it = m_driveMap.find(driveKey); it != m_driveMap.end() )
//        {
//            return it->second;
//        }
//        assert(0);
//    }

    void saveDownloadChannelMap()
    {
        std::ostringstream os( std::ios::binary );
        cereal::PortableBinaryOutputArchive archive( os );
        archive( m_dnChannelMap );

        saveRestartData((fs::path( m_storageDirectory ) / "downloadChannelMap").string(), os.str());
    }

    bool loadDownloadChannelMap()
    {
        std::string data;

        if ( !loadRestartData((fs::path( m_storageDirectory ) / "downloadChannelMap").string(), data ))
        {
            return false;
        }

        try
        {
            std::istringstream is( data, std::ios::binary );
            cereal::PortableBinaryInputArchive iarchive( is );
            iarchive( m_dnChannelMapBackup );
        }
        catch ( ... )
        {
            return false;
        }

        try
        {
            std::istringstream is( data, std::ios::binary );
            cereal::PortableBinaryInputArchive iarchive( is );
            iarchive( m_dnChannelMapBackup );
        }
        catch ( ... )
        {
            return false;
        }

        return true;
    }

    virtual bool on_dht_request( lt::string_view query,
                                 boost::asio::ip::udp::endpoint const& source,
                                 lt::bdecode_node const& message,
                                 lt::entry& response ) override
    {
        if ( isStopped())
        {
            return false;
        }

        //_LOG( "query: " << query );

        const std::set<lt::string_view> supportedQueries =
                {"opinion", "dn_opinion", "code_verify", "verify_opinion",
                 "chunk-info" //, "finish-stream"
                };
        if ( supportedQueries.contains( query )
#ifndef SKIP_GRPC
            || m_messageSubscribers.contains(std::string(query.begin(), query.end()))
#endif
            )
        {
            auto str = message.dict_find_string_value( "x" );
            std::string packet((char*) str.data(), (char*) str.data() + str.size());

            onMessageReceived( std::string( query.begin(), query.end()), packet, source );

            response["r"]["q"] = std::string( query );
            response["r"]["ret"] = "ok";
            return true;
        } else if ( query == "get_dn_rcpts" )
        {
            // extract signature
            auto sign = message.dict_find_string_value( "sign" );
            Signature signature;
            if ( sign.size() != signature.size())
            {
                __LOG_WARN( "invalid sign size" )
                return true;
            }
            memcpy( signature.data(), sign.data(), signature.size());

            // extract message
            auto str = message.dict_find_string_value( "x" );

            // extract request fields
            //
            ReplicatorKey senderKey;
            DriveKey driveKey;
            ChannelId channelId;

            if ( str.size() != senderKey.size() + driveKey.size() + channelId.size() )
            {
                __LOG_WARN( "invalid respose size" )
                return true;
            }

            uint8_t* ptr = (uint8_t*) str.data();
            memcpy( senderKey.data(), ptr, senderKey.size());
            ptr += senderKey.size();
            memcpy( driveKey.data(), ptr, driveKey.size());
            ptr += driveKey.size();
            memcpy( channelId.data(), ptr, channelId.size());

            if ( !crypto::Verify( senderKey, {utils::RawBuffer{(const uint8_t*) str.data(), str.size()}}, signature ))
            {
                __LOG_WARN( "invalid respose size" )
                return true;
            }


            std::ostringstream os( std::ios::binary );
            Signature responseSignature;
            if ( createSyncRcpts( driveKey, channelId, os, responseSignature ))
            {
                //_LOG( "response[r][q]: " << query );
                response["r"]["q"] = std::string( query );
                response["r"]["ret"] = os.str();
                response["r"]["sign"] = std::string( responseSignature.begin(), responseSignature.end());
            }
            return true;
        } else if ( query == "rcpt" )
        {
            auto str = message.dict_find_string_value( "x" );

            RcptMessage msg( str.data(), str.size());

            if ( !msg.isValidSize())
            {
                __LOG( "WARNING!!!: invalid rcpt size" )
                return false;
            }

            acceptReceiptFromAnotherReplicator( msg );

            response["r"]["q"] = std::string( query );
            response["r"]["ret"] = "ok";
            return true;
        } else if ( query == "get-chunks-info" ) // message from 'viewer'
        {
            try
            {
                //std::string message( query.begin(), query.end() );
                auto str = message.dict_find_string_value( "x" );
                std::string packet((char*) str.data(), (char*) str.data() + str.size());

                std::istringstream is( packet, std::ios::binary );
                cereal::PortableBinaryInputArchive iarchive( is );
                std::array<uint8_t, 32> driveKey;
                iarchive( driveKey );

                if ( auto driveIt = m_driveMap.find( driveKey ); driveIt != m_driveMap.end())
                {
                    std::array<uint8_t, 32> streamId;
                    iarchive( streamId );
                    uint32_t chunkIndex;
                    iarchive( chunkIndex );

                    std::string result = driveIt->second->acceptGetChunksInfoMessage( streamId, chunkIndex, source );
                    _LOG( "result.size: " << result.size() );
                    std::string hexString;
                    boost::algorithm::hex( result.begin(), result.end(), std::back_inserter(hexString) );
                    _LOG( "result.hexString: " << hexString );
                    if ( !result.empty())
                    {
                        response["r"]["q"] = std::string( query );
                        response["r"]["ret"] = result;
                    }
                    return true;
                } else
                {
                    _LOG_WARN( "Unknown drive: " << Key( driveKey ))
                }
            }
            catch ( ... )
            {_LOG_WARN( "execption occured" )}

            return true;
        } else if ( query == "get-playlist-hash" ) // message from 'viewer'
        {
            try
            {
                //std::string message( query.begin(), query.end() );
                auto str = message.dict_find_string_value( "x" );
                std::string packet((char*) str.data(), (char*) str.data() + str.size());

                std::istringstream is( packet, std::ios::binary );
                cereal::PortableBinaryInputArchive iarchive( is );
                std::array<uint8_t, 32> driveKey;
                iarchive( driveKey );

                if ( auto driveIt = m_driveMap.find( driveKey ); driveIt != m_driveMap.end())
                {
                    std::array<uint8_t, 32> streamId;
                    iarchive( streamId );

                    std::string result = driveIt->second->acceptGetPlaylistHashRequest( streamId );
                    if ( !result.empty())
                    {
                        response["r"]["q"] = std::string( query );
                        response["r"]["ret"] = result;
                    }
                    return true;
                } else
                {
                    _LOG_WARN( "Unknown drive: " << Key( driveKey ))
                }
            }
            catch ( ... )
            {_LOG_WARN( "execption occured" )}

            return true;
        }

        else if ( query == "get_channel_status" )
        {
            // extract signature
            auto sign = message.dict_find_string_value("sign");
            Signature signature;
            if ( sign.size() != signature.size() )
            {
                __LOG_WARN( "invalid sign size" )
                return true;
            }
            memcpy( signature.data(), sign.data(), signature.size() );

            // extract message
            auto str = message.dict_find_string_value("x");

            // extract request fields
            //
            ClientKey senderKey;
            DriveKey  driveKey;
            ChannelId channelId;

            if ( str.size() != senderKey.size() + driveKey.size() + channelId.size() )
            {
                __LOG_WARN( "invalid respose size" )
                return true;
            }

            uint8_t* ptr = (uint8_t*)str.data();
            memcpy( senderKey.data(), ptr, senderKey.size() );
            ptr += senderKey.size();
            memcpy( driveKey.data(), ptr, driveKey.size() );
            ptr += driveKey.size();
            memcpy( channelId.data(), ptr, channelId.size() );

            if ( ! crypto::Verify( senderKey, { utils::RawBuffer{(const uint8_t*) str.data(), str.size()} }, signature ) )
            {
                __LOG_WARN( "invalid signature" )
                return true;
            }

            std::ostringstream os( std::ios::binary );
            Signature responseSignature;
            if ( ! createChannelStatus( driveKey, channelId, os, responseSignature ) )
            {
                response["r"]["not_found"] = "yes";
            }
            response["r"]["q"] = std::string(query);
            response["r"]["ret"] = os.str();
            response["r"]["sign"] = std::string( responseSignature.begin(), responseSignature.end() );

            return true;
        }

        else if ( query == "get_modification_status" )
        {
            // extract signature
            auto sign = message.dict_find_string_value("sign");
            Signature signature;
            if ( sign.size() != signature.size() )
            {
                __LOG_WARN( "invalid sign size" )
                return true;
            }
            memcpy( signature.data(), sign.data(), signature.size() );

            // extract message
            auto str = message.dict_find_string_value("x");

            // extract request fields
            //
            ClientKey senderKey;
            DriveKey  driveKey;
            Hash256   modifyTx;

            if ( str.size() != senderKey.size() + driveKey.size() + modifyTx.size() )
            {
                __LOG_WARN( "invalid respose size" )
                return true;
            }

            uint8_t* ptr = (uint8_t*)str.data();
            memcpy( senderKey.data(), ptr, senderKey.size() );
            ptr += senderKey.size();
            memcpy( driveKey.data(), ptr, driveKey.size() );
            ptr += driveKey.size();
            memcpy( modifyTx.data(), ptr, modifyTx.size() );

            if ( ! crypto::Verify( senderKey, { utils::RawBuffer{(const uint8_t*) str.data(), str.size()} }, signature ) )
            {
                __LOG_WARN( "invalid signature" )
                return true;
            }

            auto drive = getDrive( driveKey );
            if ( drive )
            {
                bool isModificationQueued = false;
                auto currentTask = drive->getDriveStatus( modifyTx.array(), isModificationQueued );
                if ( currentTask )
                {
                    response["r"]["currentTask"] = driveTaskTypeToString(*currentTask);
                }
                if ( isModificationQueued )
                {
                    response["r"]["taskIsQueued"] = "yes";
                }
            }

            std::ostringstream os( std::ios::binary );
            Signature responseSignature;
            bool isModificationFinished = false;
            if ( ! createModificationStatus( driveKey, modifyTx, os, responseSignature, isModificationFinished ) )
            {
                response["r"]["not_found"] = "yes";
            }

            response["r"]["q"] = std::string(query);
            response["r"]["ret"] = os.str();
            response["r"]["sign"] = std::string( responseSignature.begin(), responseSignature.end() );

            if ( isModificationFinished )
            {
                response["r"]["taskIsFinished"] = "yes";
            }
            return true;
        }

        else if ( query == "get_stream_status" )
        {
            //std::string message( query.begin(), query.end() );
            auto str = message.dict_find_string_value("x");
            std::string packet( (char*)str.data(), (char*)str.data()+str.size() );

            // get drive key
            std::istringstream is( packet, std::ios::binary );
            cereal::PortableBinaryInputArchive iarchive(is);
            std::array<uint8_t,32> driveKey;
            iarchive( driveKey );

            if ( auto driveIt = m_driveMap.find( driveKey ); driveIt != m_driveMap.end() )
            {
                // set response
                std::string status = driveIt->second->getStreamStatus();
                response["r"]["q"] = std::string(query);
                response["r"]["ret"] = status;
                return true;
            }
            else
            {
                _LOG_WARN( "Unknown drive: " << Key(driveKey) )
            }
            return true;
        }

        return false;
    }

    void handleDhtResponse( lt::bdecode_node response, boost::asio::ip::udp::endpoint /*endpoint*/ ) override
    {
        try
        {
            auto rDict = response.dict_find_dict( "r" );
            auto query = rDict.dict_find_string_value( "q" );
            if ( query == "get_dn_rcpts" )
            {
                lt::string_view rsp = rDict.dict_find_string_value( "ret" );
                lt::string_view sign = rDict.dict_find_string_value( "sign" );
                onSyncRcptReceived( rsp, sign );
            }
        }
        catch ( ... )
        {
        }
    }

    void dbgSetLogMode( uint8_t mode ) override
    {
        _FUNC_ENTRY

        boost::asio::post(m_session->lt_session().get_context(), [=,this]() mutable
        {
            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            m_session->setLogMode( static_cast<LogMode>(mode) );
        });
    }

    void dbgAllowCreateNonExistingDrives() override
    {
        m_dbgAllowCreateNonExistingDrives = true;
    }

    void asyncDbgAddVirtualDrive( Key driveKey )
    {
        _FUNC_ENTRY

        boost::asio::post( m_session->lt_session().get_context(), [=, this]() mutable
        {

            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }
            if ( m_driveMap.find( driveKey ) != m_driveMap.end())
            {
                return;
            }

            AddDriveRequest driveRequest;
            driveRequest.m_client = randomByteArray<Key>();
            driveRequest.m_driveSize = 10ULL * 1024ULL * 1024ULL * 1024ULL;
            driveRequest.m_expectedCumulativeDownloadSize = 0;

            auto drive = sirius::drive::createDefaultFlatDrive(
                    session(),
                    m_storageDirectory,
                    driveKey,
                    driveRequest.m_client,
                    driveRequest.m_driveSize,
                    driveRequest.m_expectedCumulativeDownloadSize,
                    std::move( driveRequest.m_completedModifications ),
                    m_eventHandler,
                    *this,
                    driveRequest.m_fullReplicatorList,
                    driveRequest.m_modifyDonatorShard,
                    driveRequest.m_modifyRecipientShard,
                    m_dbgEventHandler );

            m_driveMap[driveKey] = drive;

            m_session->startSearchPeerEndpoints( driveRequest.m_fullReplicatorList );
            m_session->addClientToLocalEndpointMap( driveRequest.m_client );

            std::cout << "added virtualDrive " << driveKey;
        } );//post
    }

private:
    std::shared_ptr<sirius::drive::Session> session()
    {
        return m_session;
    }

    void removeUnusedDrives( const std::string& path )
    {
        DBG_MAIN_THREAD

        // Despite the fact we work on the main thread,
        // it is ok to work with fs since it is done once during the Replicator initialization

        auto rootFolderPath = fs::path( path );

        std::set<fs::path> toRemove;

        {
            std::error_code ec;
            if ( !std::filesystem::is_directory( rootFolderPath, ec ))
            {
                return;
            }
        }

        try
        {
            for ( const auto& entry : std::filesystem::directory_iterator( rootFolderPath ))
            {
                if ( entry.is_directory())
                {
                    const auto entryName = entry.path().filename().string();

                    auto driveKey = stringToByteArray<Key>( entryName );

                    if ( !m_driveMap.contains( driveKey ))
                    {
                        {
                            toRemove.insert( entry.path());
                        }
                    }
                }
            }
        }
        catch ( ... )
        {
            _LOG_WARN( "Invalid Attempt To Iterate " << rootFolderPath );
        }

        for ( const auto& p: toRemove )
        {
            std::error_code ec;
            fs::remove_all( p, ec );
        }
    }

public:

    void initiateManualModifications( const DriveKey& driveKey, const InitiateModificationsRequest& request ) override
    {
        _FUNC_ENTRY

        if (m_dbgAllowCreateNonExistingDrives) {
            asyncDbgAddVirtualDrive(driveKey);
        }

        boost::asio::post( m_session->lt_session().get_context(), [=, this]() mutable
        {
            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            auto driveIt = m_driveMap.find( driveKey );

            if ( driveIt == m_driveMap.end())
            {
                request.m_callback( {} );
                return;
            }

            driveIt->second->initiateManualModifications( std::make_unique<InitiateModificationsRequest>(request) );

        } );
    }

    void initiateManualSandboxModifications( const DriveKey& driveKey,
                                             const InitiateSandboxModificationsRequest& request ) override
    {
        _FUNC_ENTRY

        boost::asio::post( m_session->lt_session().get_context(), [=, this]() mutable
        {
            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            auto driveIt = m_driveMap.find( driveKey );

            if ( driveIt == m_driveMap.end())
            {
                request.m_callback( {} );
                return;
            }

            driveIt->second->initiateManualSandboxModifications( std::make_unique<InitiateSandboxModificationsRequest>(request) );

        } );
    }

    void openFile( const DriveKey& driveKey, const OpenFileRequest& request ) override
    {
        _FUNC_ENTRY

        boost::asio::post( m_session->lt_session().get_context(), [=, this]() mutable
        {
            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            auto driveIt = m_driveMap.find( driveKey );

            if ( driveIt == m_driveMap.end())
            {
                request.m_callback( {} );
                return;
            }

            driveIt->second->openFile( std::make_unique<OpenFileRequest>(request) );

        } );
    }

    void writeFile( const DriveKey& driveKey, const WriteFileRequest& request ) override
    {
        _FUNC_ENTRY

        boost::asio::post( m_session->lt_session().get_context(), [=, this]() mutable
        {
            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            auto driveIt = m_driveMap.find( driveKey );

            if ( driveIt == m_driveMap.end())
            {
                request.m_callback( {} );
                return;
            }

            driveIt->second->writeFile( std::make_unique<WriteFileRequest>(request) );

        } );
    }

    void readFile( const DriveKey& driveKey, const ReadFileRequest& request ) override
    {
        _FUNC_ENTRY

        boost::asio::post( m_session->lt_session().get_context(), [=, this]() mutable
        {
            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            auto driveIt = m_driveMap.find( driveKey );

            if ( driveIt == m_driveMap.end())
            {
                request.m_callback( {} );
                return;
            }

            driveIt->second->readFile( std::make_unique<ReadFileRequest>(request) );

        } );
    }

    void flush( const DriveKey& driveKey, const FlushRequest& request ) override
    {
        _FUNC_ENTRY

        boost::asio::post( m_session->lt_session().get_context(), [=, this]() mutable
        {
            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            auto driveIt = m_driveMap.find( driveKey );

            if ( driveIt == m_driveMap.end())
            {
                request.m_callback( {} );
                return;
            }

            driveIt->second->flush( std::make_unique<FlushRequest>(request) );

        } );
    }

    void closeFile( const DriveKey& driveKey, const CloseFileRequest& request ) override
    {
        _FUNC_ENTRY

        boost::asio::post( m_session->lt_session().get_context(), [=, this]() mutable
        {
            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            auto driveIt = m_driveMap.find( driveKey );

            if ( driveIt == m_driveMap.end())
            {
                request.m_callback( {} );
                return;
            }

            driveIt->second->closeFile( std::make_unique<CloseFileRequest>(request) );

        } );
    }

    void removeFsTreeEntry( const DriveKey& driveKey, const RemoveFilesystemEntryRequest& request ) override
    {
        _FUNC_ENTRY

        boost::asio::post( m_session->lt_session().get_context(), [=, this]() mutable
        {
            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            auto driveIt = m_driveMap.find( driveKey );

            if ( driveIt == m_driveMap.end())
            {
                request.m_callback( {} );
                return;
            }

            driveIt->second->removeFsTreeEntry( std::make_unique<RemoveFilesystemEntryRequest>(request) );

        } );
    }

    void pathExist( const DriveKey& driveKey, const PathExistRequest& request ) override {
        _FUNC_ENTRY

        boost::asio::post( m_session->lt_session().get_context(), [=, this]() mutable
        {
            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            auto driveIt = m_driveMap.find( driveKey );

            if ( driveIt == m_driveMap.end())
            {
                request.m_callback( {} );
                return;
            }

            driveIt->second->pathExist( std::make_unique<PathExistRequest>(request) );

        } );
    }

    void pathIsFile( const DriveKey& driveKey, const PathIsFileRequest& request ) override {
        _FUNC_ENTRY

        boost::asio::post( m_session->lt_session().get_context(), [=, this]() mutable
        {
            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            auto driveIt = m_driveMap.find( driveKey );

            if ( driveIt == m_driveMap.end())
            {
                request.m_callback( {} );
                return;
            }

            driveIt->second->pathIsFile( std::make_unique<PathIsFileRequest>(request) );

        } );
    }

    void fileSize( const DriveKey& driveKey, const FileSizeRequest& request ) override
    {
        _FUNC_ENTRY

        boost::asio::post( m_session->lt_session().get_context(), [=, this]() mutable
        {
            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            auto driveIt = m_driveMap.find( driveKey );

            if ( driveIt == m_driveMap.end())
            {
                request.m_callback( {} );
                return;
            }

            driveIt->second->fileSize( std::make_unique<FileSizeRequest>(request) );

        } );
    }

    void createDirectories( const DriveKey& driveKey, const CreateDirectoriesRequest& request ) override
    {
        _FUNC_ENTRY

        boost::asio::post( m_session->lt_session().get_context(), [=, this]() mutable
        {
            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            auto driveIt = m_driveMap.find( driveKey );

            if ( driveIt == m_driveMap.end())
            {
                request.m_callback( {} );
                return;
            }

            driveIt->second->createDirectories( std::make_unique<CreateDirectoriesRequest>(request) );

        } );
    }

    void folderIteratorCreate( const DriveKey& driveKey, const FolderIteratorCreateRequest& request ) override
    {
        _FUNC_ENTRY

        boost::asio::post( m_session->lt_session().get_context(), [=, this]() mutable
        {
            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            auto driveIt = m_driveMap.find( driveKey );

            if ( driveIt == m_driveMap.end())
            {
                request.m_callback( {} );
                return;
            }

            driveIt->second->folderIteratorCreate( std::make_unique<FolderIteratorCreateRequest>(request) );

        } );
    }

    void folderIteratorDestroy( const DriveKey& driveKey, const FolderIteratorDestroyRequest& request ) override
    {
        _FUNC_ENTRY

        boost::asio::post( m_session->lt_session().get_context(), [=, this]() mutable
        {
            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            auto driveIt = m_driveMap.find( driveKey );

            if ( driveIt == m_driveMap.end())
            {
                request.m_callback( {} );
                return;
            }

            driveIt->second->folderIteratorDestroy( std::make_unique<FolderIteratorDestroyRequest>(request) );

        } );
    }

    void folderIteratorHasNext( const DriveKey& driveKey, const FolderIteratorHasNextRequest& request ) override
    {
        _FUNC_ENTRY

        boost::asio::post( m_session->lt_session().get_context(), [=, this]() mutable
        {
            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            auto driveIt = m_driveMap.find( driveKey );

            if ( driveIt == m_driveMap.end())
            {
                request.m_callback( {} );
                return;
            }

            driveIt->second->folderIteratorHasNext( std::make_unique<FolderIteratorHasNextRequest>(request) );

        } );
    }

    void folderIteratorNext( const DriveKey& driveKey, const FolderIteratorNextRequest& request ) override
    {
        _FUNC_ENTRY

        boost::asio::post( m_session->lt_session().get_context(), [=, this]() mutable
        {
            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            auto driveIt = m_driveMap.find( driveKey );

            if ( driveIt == m_driveMap.end())
            {
                request.m_callback( {} );
                return;
            }

            driveIt->second->folderIteratorNext( std::make_unique<FolderIteratorNextRequest>(request) );

        } );
    }

    void moveFsTreeEntry( const DriveKey& driveKey, const MoveFilesystemEntryRequest& request ) override
    {
        _FUNC_ENTRY

        boost::asio::post( m_session->lt_session().get_context(), [=, this]() mutable
        {
            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            auto driveIt = m_driveMap.find( driveKey );

            if ( driveIt == m_driveMap.end())
            {
                request.m_callback( {} );
                return;
            }

            driveIt->second->moveFsTreeEntry( std::make_unique<MoveFilesystemEntryRequest>(request) );

        } );
    }

    void applySandboxManualModifications( const DriveKey& driveKey,
                                          const ApplySandboxModificationsRequest& request ) override
    {
        _FUNC_ENTRY

        boost::asio::post( m_session->lt_session().get_context(), [=, this]() mutable
        {
            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            auto driveIt = m_driveMap.find( driveKey );

            if ( driveIt == m_driveMap.end())
            {
                request.m_callback( {} );
                return;
            }

            driveIt->second->applySandboxManualModifications( std::make_unique<ApplySandboxModificationsRequest>(request) );

        } );
    }

    void evaluateStorageHash( const DriveKey& driveKey, const EvaluateStorageHashRequest& request ) override
    {
        _FUNC_ENTRY

        boost::asio::post( m_session->lt_session().get_context(), [=, this]() mutable
        {
            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            auto driveIt = m_driveMap.find( driveKey );

            if ( driveIt == m_driveMap.end())
            {
                request.m_callback( {} );
                return;
            }

            driveIt->second->evaluateStorageHash( std::make_unique<EvaluateStorageHashRequest>(request) );

        } );
    }

    void applyStorageManualModifications( const DriveKey& driveKey,
                                          const ApplyStorageModificationsRequest& request ) override
    {
        _FUNC_ENTRY

        boost::asio::post( m_session->lt_session().get_context(), [=, this]() mutable
        {
            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            auto driveIt = m_driveMap.find( driveKey );

            if ( driveIt == m_driveMap.end())
            {
                request.m_callback( {} );
                return;
            }

            driveIt->second->applyStorageManualModifications( std::make_unique<ApplyStorageModificationsRequest>(request) );

        } );
    }

    void manualSynchronize( const DriveKey& driveKey, const SynchronizationRequest& request ) override
    {
        _FUNC_ENTRY

        boost::asio::post( m_session->lt_session().get_context(), [=, this]() mutable
        {
            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            auto driveIt = m_driveMap.find( driveKey );

            if ( driveIt == m_driveMap.end())
            {
                request.m_callback( {} );
                return;
            }

            driveIt->second->manualSynchronize( std::make_unique<SynchronizationRequest>(request) );

        } );
    }

    void getFileInfo( const DriveKey& driveKey, const FileInfoRequest& request ) override
    {
        _FUNC_ENTRY

        boost::asio::post( m_session->lt_session().get_context(), [=, this]() mutable
        {
            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            auto driveIt = m_driveMap.find( driveKey );

            if ( driveIt == m_driveMap.end())
            {
                request.m_callback( {} );
                return;
            }

            driveIt->second->getFileInfo( std::make_unique<FileInfoRequest>(request) );

        } );
    }

    void getActualModificationId( const DriveKey& driveKey, const ActualModificationIdRequest& request ) override
    {
        _FUNC_ENTRY

        boost::asio::post( m_session->lt_session().get_context(), [=, this]() mutable
        {
            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            auto driveIt = m_driveMap.find( driveKey );

            if ( driveIt == m_driveMap.end())
            {
                request.m_callback( {} );
                return;
            }

            driveIt->second->getActualModificationId( std::make_unique<ActualModificationIdRequest>(request) );

        } );
    }

    void getFilesystem( const DriveKey& driveKey, const FilesystemRequest& request ) override
    {
        _FUNC_ENTRY

        boost::asio::post( m_session->lt_session().get_context(), [=, this]() mutable
        {
            DBG_MAIN_THREAD

            if ( m_isDestructing )
            {
                return;
            }

            auto driveIt = m_driveMap.find( driveKey );

            if ( driveIt == m_driveMap.end())
            {
                request.m_callback( {} );
                return;
            }

            driveIt->second->getFilesystem( request );

        } );
    }

#ifndef SKIP_GRPC
    void setServiceAddress( const std::string& address ) override {
        m_serviceServerAddress = address;
        _LOG( "Listen RPC Services On" << address )
    }

    void enableSupercontractServer() override {
        auto service = std::make_shared<contract::StorageServer>( weak_from_this() );
        m_services.emplace_back(std::move(service));
    }

    void enableMessengerServer() override {
        auto service = messenger::MessengerServerBuilder().build(weak_from_this());
        m_services.emplace_back(std::move(service));
    }

    void sendMessage( const messenger::OutputMessage& message ) override {

        DBG_MAIN_THREAD

        _LOG( "Message Sending Is Requested " << message.m_tag )

        sendMessage(message.m_tag, message.m_receiver, message.m_data);
    }

    void subscribe( const std::string& tag, std::shared_ptr<messenger::MessageSubscriber> subscriber ) override {

        DBG_MAIN_THREAD
        m_messageSubscribers[tag] = std::move(subscriber);
        _LOG( "Subscription To " << tag << " Is Requested" )
    }
#endif //#ifndef SKIP_GRPC
    
    virtual void addReplicatorKeyToKademlia( const Key& key ) override
    {
        m_session->addReplicatorKeyToKademlia(key);
    }
    
    virtual void addReplicatorKeysToKademlia( const std::vector<Key>& keys ) override
    {
        m_session->addReplicatorKeysToKademlia(keys);
    }
    
    virtual void removeReplicatorKeyFromKademlia( const Key& keys ) override
    {
        m_session->removeReplicatorKeyFromKademlia(keys);
    }

    virtual void dbgTestKademlia( KademliaDbgFunc dbgFunc ) override
    {
        m_session->dbgTestKademlia(dbgFunc);
    }

    virtual void dbgTestKademlia2( ReplicatorList& outReplicatorList ) override
    {
        boost::asio::post( m_session->lt_session().get_context(), [outReplicatorList=outReplicatorList,this]() mutable {
            for( auto& [key,drive] : m_driveMap )
            {
                drive->dbgTestKademlia2( outReplicatorList );
            }
            int counter = 0;
            for( auto& key : outReplicatorList )
            {
                int port = -1;
                if ( ! dbgGetEndpoint(key,port).has_value() )
                {
                    ___LOG( m_port << " ????????????????? dbgTestKademlia2: " << port << " " << key );
                    counter++;
                }
            }
            ___LOG( m_port << " =========================== dbgTestKademlia2: " << counter << " of: " << outReplicatorList.size() );
        } );
    }

    OptionalEndpoint dbgGetEndpoint( const Key& key, int& port )
    {
        if ( auto ep = m_session->getEndpoint(key); ep )
        {
            port = ep->port();
            return ep;
        }
//        if ( auto info = m_session->getPeerInfo(key); info != nullptr )
//        {
//            return info->endpoint();
//        }
        return {};
    }

//    virtual OptionalEndpoint dbgGetEndpoint( const Key& key, int& port )
//    {
//        auto* info = m_session->getPeerInfo(key);
//        if ( info != nullptr )
//        {
//            return info->endpoint();
//        }
//        if ( auto ep = m_session->getEndpoint(key); ep )
//        {
//            port = ep->port();
//        }
//        return {};
//    }

};

std::shared_ptr<Replicator> createDefaultReplicator(
        const crypto::KeyPair& keyPair,
        std::string address,
        std::string port,
		std::string wsPort,
        std::string storageDirectory,
        //std::string&& sandboxDirectory,
        const std::vector<ReplicatorInfo>& bootstraps,
        bool useTcpSocket,
        ReplicatorEventHandler& handler,
        DbgReplicatorEventHandler* dbgEventHandler,
        const std::string& dbgReplicatorName,
        std::string  logOptions )
{
    return std::make_shared<DefaultReplicator>(
            keyPair,
            address,
            port,
			wsPort,
            storageDirectory,
            useTcpSocket,
            handler,
            dbgEventHandler,
            bootstraps,
            dbgReplicatorName,
            logOptions );
}

}

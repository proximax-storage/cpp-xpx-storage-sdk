/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "types.h"
#include "drive/log.h"
#include "drive/FlatDrive.h"
#include "drive/Utils.h"
#include "drive/Session.h"
#include "drive/DownloadLimiter.h"
#include "drive/EndpointsManager.h"
#include "DnOpinionSyncronizer.h"

#include <cereal/types/vector.hpp>
#include <cereal/types/array.hpp>
#include <cereal/types/map.hpp>
#include <cereal/archives/portable_binary.hpp>

#include <libtorrent/alert_types.hpp>

#include <filesystem>
#include <mutex>
#include <future>

namespace sirius::drive {

#define CHANNELS_NOT_OWNED_BY_DRIVES

//
// DefaultReplicator
//
class DefaultReplicator : public DownloadLimiter // Replicator
{
private:
    boost::asio::io_context m_replicatorContext;
    std::thread             m_libtorrentThread;
    
    // Session listen interface
    std::string m_address;
    std::string m_port;

    // Folders for drives and sandboxes
    std::string m_storageDirectory;
    std::string m_sandboxDirectory;
    
    int         m_downloadApprovalTransactionTimerDelayMs = 10 * 1000;
    int         m_modifyApprovalTransactionTimerDelayMs   = 10 * 1000;
    int         m_verifyCodeTimerDelayMs                  = 5 * 60 * 1000;
    int         m_verifyApprovalTransactionTimerDelayMs   = 10 * 1000;
    int         m_shareMyDownloadOpinionTimerDelayMs      = 5 * 60 * 1000;


    bool        m_replicatorIsDestructing = false;

    bool        m_useTcpSocket;
    
    ReplicatorEventHandler& m_eventHandler;
    DbgReplicatorEventHandler*  m_dbgEventHandler;

    EndpointsManager        m_endpointsManager;
    DnOpinionSyncronizer    m_dnOpinionSyncronizer;

    // key is verify tx
    std::map<std::array<uint8_t,32>, VerifyOpinion> m_verifyApprovalMap;

public:
    DefaultReplicator (
               const crypto::KeyPair& keyPair,
               std::string&& address,
               std::string&& port,
               std::string&& storageDirectory,
               std::string&& sandboxDirectory,
               bool          useTcpSocket,
               ReplicatorEventHandler& handler,
               DbgReplicatorEventHandler*  dbgEventHandler,
               const std::vector<ReplicatorInfo>& bootstraps,
               const char*   dbgReplicatorName ) : DownloadLimiter( keyPair, dbgReplicatorName ),

        m_address( std::move(address) ),
        m_port( std::move(port) ),
        m_storageDirectory( std::move(storageDirectory) ),
        m_sandboxDirectory( std::move(sandboxDirectory) ),
        m_useTcpSocket( useTcpSocket ),
        m_eventHandler( handler ),
        m_dbgEventHandler( dbgEventHandler ),
        m_endpointsManager( *this, bootstraps, m_dbgOurPeerName),
        m_dnOpinionSyncronizer( *this )
    {
    }

    bool isStopped() override
    {
        DBG_MAIN_THREAD

        return m_replicatorIsDestructing;
    }

    void stop()
    {
        DBG_MAIN_THREAD

        m_replicatorIsDestructing = true;

        m_dnOpinionSyncronizer.stop();

        for( auto& [key,drive]: m_driveMap )
        {
            drive->terminate();
        }

        for ( auto& [channelId, value]: m_downloadChannelMap )
        {
            for ( auto& [event, opinion]: value.m_downloadOpinionMap )
            {
                opinion.m_timer.reset();
                opinion.m_opinionShareTimer.reset();
            }
        }

        m_endpointsManager.stop();
    }

    virtual ~DefaultReplicator()
    {
#ifdef DEBUG_OFF_CATAPULT
        _LOG( "~DefaultReplicator() ")
#endif

       boost::asio::post(m_session->lt_session().get_context(), [this]() mutable {

           DBG_MAIN_THREAD

            stop();
        });

       m_session->endSession();

       auto blockedDestructor = m_session->lt_session().abort();
       m_session.reset();

       if ( m_libtorrentThread.joinable() )
       {
           m_libtorrentThread.join();
       }

       saveDownloadChannelMap();
    }
    
    void start() override
    {
        endpoint_list bootstrapEndpoints;
        for ( const auto& info: m_endpointsManager.getBootstraps() )
        {
            bootstrapEndpoints.push_back( info.m_endpoint );
        }

        m_session = createDefaultSession( m_replicatorContext, m_address + ":" + m_port, [port=m_port,this] (const lt::alert* pAlert)
                                         {
                                             if ( pAlert->type() == lt::listen_failed_alert::alert_type )
                                             {
                                                 _LOG_WARN( "Replicator session alert: " << pAlert->message() );
                                                 _LOG_WARN( "Port is busy?: " << port );
                                                 m_eventHandler.onLibtorrentSessionError( pAlert->message() );
                                             }
                                         },
                                         weak_from_this(),
                                         weak_from_this(),
                                         bootstrapEndpoints,
                                         m_useTcpSocket);

        m_session->lt_session().m_dbgOurPeerName = m_dbgOurPeerName.c_str();
        
        m_libtorrentThread = std::thread( [this] {
            m_replicatorContext.run();
#ifdef DEBUG_OFF_CATAPULT
            _LOG( "libtorrentThread ended" );
#endif
        });

        boost::asio::post(m_session->lt_session().get_context(), [=,this]() mutable {

            DBG_MAIN_THREAD

            m_endpointsManager.start(m_session);
        });

        m_dbgThreadId = m_libtorrentThread.get_id();

        removeDriveDataOfBrokenClose();
        loadDownloadChannelMap();

        m_dnOpinionSyncronizer.start( m_session );
    }
    
    void removeDriveDataOfBrokenClose()
    {
        auto rootFolderPath = fs::path( m_storageDirectory );

        std::error_code ec;
        if ( !std::filesystem::is_directory(rootFolderPath,ec) )
            return;

        for( const auto& entry : std::filesystem::directory_iterator(rootFolderPath) )
        {
            if ( entry.is_directory() )
            {
                const auto entryName = entry.path().filename().string();
                
                std::error_code errorCode;
                if ( fs::exists( FlatDrive::driveIsClosingPath( rootFolderPath / entryName ), errorCode ) )
                {
                    fs::remove_all( rootFolderPath / entryName );
                }
            }
        }
    }

    Hash256 dbgGetRootHash( const Key& driveKey ) override
    {
        std::promise<Hash256> thePromise;
        auto future = thePromise.get_future();
        
        boost::asio::post(m_session->lt_session().get_context(), [=,&thePromise,this]()
        {
            DBG_MAIN_THREAD
            
            const auto drive = getDrive(driveKey);
            _ASSERT( drive );
            auto rootHash = drive->rootHash();
            thePromise.set_value( rootHash );
        });
        
        return future.get();
    }

    void dbgPrintDriveStatus( const Key& driveKey ) override
    {
        boost::asio::post(m_session->lt_session().get_context(), [=,this]()
        {
            DBG_MAIN_THREAD

            if ( const auto drive = getDrive(driveKey); drive )
            {
                return drive->dbgPrintDriveStatus();
            }

            _LOG_ERR( "unknown dive: " << driveKey );
            throw std::runtime_error( std::string("unknown dive: ") + toString(driveKey.array()) );
        });

    }

    void asyncInitializationFinished() override
    {}

    void asyncAddDrive( Key driveKey, AddDriveRequest driveRequest ) override
    {
        boost::asio::post(m_session->lt_session().get_context(), [=,this]() mutable {
        
            DBG_MAIN_THREAD

            if ( m_replicatorIsDestructing )
            {
                return;
            }

            _LOG( "adding drive " << driveKey );

            if (m_driveMap.find(driveKey) != m_driveMap.end()) {
                _LOG( "drive already added" );
                return;
            }

            // Exclude itself from replicator list
            for( auto it = driveRequest.m_replicators.begin();  it != driveRequest.m_replicators.end(); it++ )
            {
                if ( *it == publicKey() )
                {
                    driveRequest.m_replicators.erase( it );
                    break;
                }
            }

            auto drive = sirius::drive::createDefaultFlatDrive(
                    session(),
                    m_storageDirectory,
                    m_sandboxDirectory,
                    driveKey,
                    driveRequest.m_client,
                    driveRequest.m_driveSize,
                    driveRequest.m_expectedCumulativeDownloadSize,
                    m_eventHandler,
                    *this,
                    driveRequest.m_replicators,
                    m_dbgEventHandler );

            m_driveMap[driveKey] = drive;

            m_endpointsManager.addEndpointsEntries( driveRequest.m_replicators );
            m_endpointsManager.addEndpointEntry( driveRequest.m_client, false );

            // Notify
            if ( m_dbgEventHandler )
            {
                m_dbgEventHandler->driveAdded(drive->drivePublicKey());
            }
        });//post
    }

    void asyncRemoveDrive( Key driveKey ) override
    {
        boost::asio::post(m_session->lt_session().get_context(), [=,this]() mutable
        {
            DBG_MAIN_THREAD

            //(???) What will happen after restart?
            if ( m_replicatorIsDestructing )
            {
                return;
            }

            if ( auto drive = getDrive(driveKey); drive )
            {
                drive->startDriveClosing( {} );
            }
            else
            {
                _LOG_ERR( "drive not found: " << driveKey );
                return;
            }
        });
    }

    void asyncReplicatorAdded( Key driveKey, mobj<Key>&& replicatorKey ) override
    {
        boost::asio::post(m_session->lt_session().get_context(), [=,this]() mutable
        {
            DBG_MAIN_THREAD

            if ( auto drive = getDrive(driveKey); drive )
            {
                drive->replicatorAdded( std::move(replicatorKey) );
            }
            else
            {
                _LOG_ERR( "drive not found: " << driveKey );
                return;
            }
        });
    }

    void asyncReplicatorRemoved( Key driveKey, mobj<Key>&& replicatorKey ) override
    {
        boost::asio::post(m_session->lt_session().get_context(), [=,this]() mutable
        {
            DBG_MAIN_THREAD

            if ( auto drive = getDrive(driveKey); drive )
            {
                drive->replicatorRemoved( std::move(replicatorKey) );
            }
            else
            {
                _LOG_ERR( "drive not found: " << driveKey );
                return;
            }
        });
    }


    void asyncAddShardDistributor( Key driveKey, mobj<Key>&& replicatorKey ) override
    {
        boost::asio::post(m_session->lt_session().get_context(), [=,this]() mutable
        {
            DBG_MAIN_THREAD

            if ( auto drive = getDrive(driveKey); drive )
            {
//                drive->( std::move(replicatorKey) );
            }
            else
            {
                _LOG_ERR( "drive not found: " << driveKey );
                return;
            }
        });
    }

    void asyncRemoveShardDistributor( Key driveKey, mobj<Key>&& replicatorKey ) override
    {
        boost::asio::post(m_session->lt_session().get_context(), [=,this]() mutable
        {
            DBG_MAIN_THREAD

            if ( auto drive = getDrive(driveKey); drive )
            {
//                drive->( std::move(replicatorKey) );
            }
            else
            {
                _LOG_ERR( "drive not found: " << driveKey );
                return;
            }
        });
    }

    void asyncAddShardRecipient( Key driveKey, mobj<Key>&& replicatorKey ) override
    {
        boost::asio::post(m_session->lt_session().get_context(), [=,this]() mutable
        {
            DBG_MAIN_THREAD

            if ( auto drive = getDrive(driveKey); drive )
            {
//                drive->( std::move(replicatorKey) );
            }
            else
            {
                _LOG_ERR( "drive not found: " << driveKey );
                return;
            }
        });
    }

    void asyncRemoveShardRecipient( Key driveKey, mobj<Key>&& replicatorKey ) override
    {
        boost::asio::post(m_session->lt_session().get_context(), [=,this]() mutable
        {
            DBG_MAIN_THREAD

            if ( auto drive = getDrive(driveKey); drive )
            {
//                drive->( std::move(replicatorKey) );
            }
            else
            {
                _LOG_ERR( "drive not found: " << driveKey );
                return;
            }
        });
    }

    void asyncCloseDrive( Key driveKey, Hash256 transactionHash ) override
    {
       boost::asio::post(m_session->lt_session().get_context(), [=,this]() mutable {
        
            DBG_MAIN_THREAD

            if ( m_replicatorIsDestructing )
            {
                return;
            }

            if ( auto drive = getDrive(driveKey); drive )
            {
                drive->startDriveClosing( transactionHash );
            }
            else
            {
                _LOG( "removeDrive: drive not found: " << driveKey );
                return;
            }
        });//post
    }

    void asyncModify( Key driveKey, ModificationRequest modifyRequest ) override
    {
       boost::asio::post(m_session->lt_session().get_context(), [=,this]() mutable {
        
            DBG_MAIN_THREAD

            if ( m_replicatorIsDestructing )
            {
                return;
            }

            std::shared_ptr<sirius::drive::FlatDrive> pDrive;
            {
                if ( auto drive = getDrive(driveKey); drive )
                {
                    pDrive = drive;
                }
                else {
                    _LOG( "asyncModify(): drive not found: " << driveKey );
                    return;
                }
            }

            // Add ModifyDriveInfo to DownloadLimiter
            addModifyDriveInfo( modifyRequest.m_transactionHash.array(),
                                driveKey,
                                modifyRequest.m_maxDataSize,
                                modifyRequest.m_clientPublicKey,
                                modifyRequest.m_replicators);

            for( auto it = modifyRequest.m_replicators.begin();  it != modifyRequest.m_replicators.end(); it++ )
            {
                if ( *it == publicKey() )
                {
                    modifyRequest.m_replicators.erase( it );
                    break;
                }
            }

            pDrive->startModifyDrive( std::move(modifyRequest) );
        });//post
    }
    
    void asyncCancelModify( Key driveKey, Hash256 transactionHash ) override
    {
       boost::asio::post(m_session->lt_session().get_context(), [=,this]() mutable {
        
            DBG_MAIN_THREAD

            if ( m_replicatorIsDestructing )
            {
                return;
            }

            if ( const auto drive = getDrive(driveKey); drive )
            {
                drive->cancelModifyDrive( transactionHash );
                return;
            }

            _LOG( "asyncCancelModify: unknown drive: " << driveKey );
        });//post
    }
    
    void asyncStartDriveVerification( Key driveKey, mobj<VerificationRequest>&& request ) override
    {
#ifdef ENABLE_VERIFICATIONS
       boost::asio::post(m_session->lt_session().get_context(), [=,this]() mutable {
        
            DBG_MAIN_THREAD

            if ( m_replicatorIsDestructing )
            {
                return;
            }

            if ( const auto drive = getDrive(driveKey); drive )
            {
                drive->startVerification( std::move(request) );
                return;
            }

            _LOG( "asyncStartDriveVerification: unknown drive: " << driveKey );
        });//post
#endif
    }

    void asyncCancelDriveVerification( Key driveKey, mobj<Hash256>&& tx ) override
    {
        //TODO
        boost::asio::post(m_session->lt_session().get_context(), [=,this]() mutable {
         
             DBG_MAIN_THREAD

             if ( m_replicatorIsDestructing )
             {
                 return;
             }

             if ( const auto drive = getDrive(driveKey); drive )
             {
                 drive->cancelVerification( std::move(tx) );
                 return;
             }

             _LOG( "asyncCancelDriveVerification: unknown drive: " << driveKey );
         });//post
    }

    void asyncAddDownloadChannelInfo( Key driveKey, DownloadRequest&& request, bool mustBeSyncronized ) override
    {
       boost::asio::post(m_session->lt_session().get_context(), [=,this]() mutable {
        
            DBG_MAIN_THREAD

            if ( m_replicatorIsDestructing )
            {
                return;
            }

            std::vector<std::array<uint8_t,32>> clientList;
            for( const auto& it : request.m_clients )
                clientList.push_back( it.array() );
            addChannelInfo( request.m_channelKey.array(),
                            request.m_prepaidDownloadSize,
                            driveKey,
                            request.m_replicators,
                            clientList,
                            mustBeSyncronized );

           if ( mustBeSyncronized )
           {
               if ( std::shared_ptr<sirius::drive::FlatDrive> drive = getDrive(driveKey); drive )
               {
                   auto replicators = drive->replicatorList();
                   size_t consensusThreshould = std::min( (replicators.size()*3)/2, size_t(4) );

                   m_dnOpinionSyncronizer.startSync( request.m_channelKey.array(), drive, consensusThreshould );
               }
               else
               {
                   _LOG_WARN( "unknown drive:" << driveKey );
               }
           }
        });//post
    }

    virtual DownloadChannelInfo* getDownloadChannelInfo( const std::array<uint8_t,32>& driveKey, const std::array<uint8_t,32>& downloadChannelHash ) override
    {
        DBG_MAIN_THREAD

        if ( auto infoIt = m_downloadChannelMap.find( downloadChannelHash ); infoIt != m_downloadChannelMap.end() )
        {
            if ( infoIt->second.m_driveKey != driveKey )
            {
                _LOG_ERR( "Invalid driveKey: " << Key(driveKey) << " vs: " << Key(infoIt->second.m_driveKey) );
                return nullptr;
            }
            return &infoIt->second;
        }

        return nullptr;
    }


    void asyncRemoveDownloadChannelInfo( Key driveKey, Key channelId ) override
    {
//       boost::asio::post(m_session->lt_session().get_context(), [=,this]() mutable {
//
//            DBG_MAIN_THREAD
//
//            removeChannelInfo(channelKey);
//        });
    }

    virtual void sendReceiptToOtherReplicators( const std::array<uint8_t,32>&  downloadChannelId,
                                                const std::array<uint8_t,32>&  clientPublicKey,
                                                uint64_t                       downloadedSize,
                                                const std::array<uint8_t,64>&  signature ) override
    {
        DBG_MAIN_THREAD
        
        auto replicatorPublicKey = publicKey();

//        // check receipt
//        if ( !DownloadLimiter::verifyReceipt(  downloadChannelId,
//                                               clientPublicKey,
//                                               replicatorPublicKey,
//                                               downloadedSize,
//                                               signature ) )
//        {
//            //todo log error?
//            std::cerr << "ERROR! Invalid receipt" << std::endl << std::flush;
//            assert(0);
//            return;
//        }
        
        std::vector<uint8_t> message;
        message.insert( message.end(), downloadChannelId.begin(),   downloadChannelId.end() );
        message.insert( message.end(), clientPublicKey.begin(),     clientPublicKey.end() );
        message.insert( message.end(), replicatorPublicKey.begin(), replicatorPublicKey.end() );
        message.insert( message.end(), (uint8_t*)&downloadedSize,   ((uint8_t*)&downloadedSize)+8 );
        message.insert( message.end(), signature.begin(),           signature.end() );
        
        if ( auto it = m_downloadChannelMap.find(downloadChannelId); it != m_downloadChannelMap.end() )
        {
            // go throw replictor list
            for( auto replicatorIt = it->second.m_replicatorsList2.begin(); replicatorIt != it->second.m_replicatorsList2.end(); replicatorIt++ )
            {
                if ( *replicatorIt != replicatorPublicKey )
                {
                    //_LOG( "todo++++ sendMessage(rcpt) " << m_dbgOurPeerName << " " << int(downloadChannelId[0]) );
                    sendMessage( "rcpt", replicatorIt->array(), message );
                }
            }
        }
    }

    void onEndpointDiscovered(const std::array<uint8_t, 32> &key,
                              const std::optional<boost::asio::ip::tcp::endpoint>& endpoint) override
    {
        DBG_MAIN_THREAD

        m_endpointsManager.updateEndpoint(key, endpoint);
    }

    void processHandshake( const DhtHandshake& info, const boost::asio::ip::tcp::endpoint &endpoint )
    {
        if ( info.m_toPublicKey != m_keyPair.publicKey().array() )
        {
            return;
        }

        if ( !info.Verify() )
        {
            return;
        }

        _LOG ( "Received Handshake from " << int(info.m_fromPublicKey[0]) << " at " << endpoint.address().to_string() )

        onEndpointDiscovered(info.m_fromPublicKey, endpoint );
    }

    void processEndpointRequest( const ExternalEndpointRequest& request, const boost::asio::ip::tcp::endpoint& endpoint )
    {
        if ( m_keyPair.publicKey() == request.m_requestTo )
        {
            ExternalEndpointResponse response;
            response.m_requestTo = request.m_requestTo;
            response.m_challenge = request.m_challenge;
            response.m_endpoint = *reinterpret_cast<const std::array<uint8_t, sizeof(boost::asio::ip::tcp::endpoint)> *>(&endpoint);
            response.Sign(m_keyPair);

            std::ostringstream os( std::ios::binary );
            cereal::PortableBinaryOutputArchive archive( os );
            archive( response );

            m_session->sendMessage( "endpoint_response", { endpoint.address(), endpoint.port() }, os.str() );
        }
    }

    std::optional<boost::asio::ip::tcp::endpoint> getEndpoint( const std::array<uint8_t,32>& key ) override
    {
        return m_endpointsManager.getEndpoint( key );
    }

    virtual void asyncOnDownloadOpinionReceived( DownloadApprovalTransactionInfo anOpinion ) override
    {
       boost::asio::post(m_session->lt_session().get_context(), [=,this]() mutable {

            DBG_MAIN_THREAD

            if ( m_replicatorIsDestructing )
            {
                return;
            }

            if ( anOpinion.m_opinions.size() != 1 )
            {
                _LOG_ERR( "onDownloadOpinionReceived: invalid opinion format: anOpinion.m_opinions.size() != 1" )
                return;
            }
        
            addOpinion( std::move(anOpinion) );
        });
    }

    void processDownloadOpinion( const DownloadApprovalTransactionInfo& anOpinion ) override
    {
        DBG_MAIN_THREAD
        
        m_eventHandler.downloadOpinionHasBeenReceived(*this, anOpinion);
    }
    
    DownloadOpinion createMyOpinion( const DownloadChannelInfo& info )
    {
        DBG_MAIN_THREAD

        DownloadOpinion myOpinion( publicKey() );

        for( const auto& replicatorIt : info.m_replicatorsList2 )
        {
            if ( auto downloadedIt = info.m_replicatorUploadMap.find( replicatorIt.array()); downloadedIt != info.m_replicatorUploadMap.end() )
            {
                myOpinion.m_downloadLayout.push_back( {downloadedIt->first, downloadedIt->second.m_uploadedSize} );
            }
            else if ( replicatorIt == publicKey() )
            {
                myOpinion.m_downloadLayout.push_back( { publicKey(), info.m_uploadedSize } );
            }
            else
            {
                myOpinion.m_downloadLayout.push_back( { replicatorIt.array(), 0 } );
            }
        }
        
        return myOpinion;
    }

    bool createSyncOpinion( const std::array<uint8_t,32>& driveKey, const std::array<uint8_t,32>& channelId, DownloadOpinion& opinion ) override
    {
        DBG_MAIN_THREAD

        opinion.m_replicatorKey = publicKey();
        opinion.m_downloadLayout.clear();

        if ( auto drive = getDrive( driveKey ); drive )
        {
            if ( auto channelInfoIt = m_downloadChannelMap.find(channelId); channelInfoIt != m_downloadChannelMap.end() )
            {
                DownloadChannelInfo& channelInfo = channelInfoIt->second;

                // add our uploaded size
                opinion.m_downloadLayout.push_back( { publicKey(), channelInfo.m_uploadedSize } );

                // add other uploaded sizes
                for( const auto& replicatorKey : drive->replicatorList() )
                {
                    if ( auto downloadedIt = channelInfo.m_replicatorUploadMap.find( replicatorKey.array());
                        downloadedIt != channelInfo.m_replicatorUploadMap.end() )
                    {
                        opinion.m_downloadLayout.push_back( {downloadedIt->first, downloadedIt->second.m_uploadedSize} );
                    }
                }

                // sign our opinion
                opinion.Sign( keyPair(), driveKey, channelId );

                return true;
            }
            return false;
        }

        _LOG_ERR( "drive not found" );
        return false;
    }

    void addOpinion(DownloadApprovalTransactionInfo&& opinion)
    {
        DBG_MAIN_THREAD

        //
        // remove outdated entries (by m_creationTime)
        //
        auto now = boost::posix_time::microsec_clock::universal_time();

        for (auto &[downloadChannelId, downloadChannel]: m_downloadChannelMap)
        {
            // TODO Potential performance bottleneck
            std::erase_if(downloadChannel.m_downloadOpinionMap, [&now](const auto &item)
            {
                const auto&[key, value] = item;
                return (now - value.m_creationTime).total_seconds() > 60 * 60;
            });
        }

        //
        // add opinion
        //
        auto channelIt = m_downloadChannelMap.find(opinion.m_downloadChannelId);

        if (channelIt == m_downloadChannelMap.end())
        {
            _LOG_WARN("Attempt to add opinion for a non-existing channel");
            return;
        }

        auto &channel = channelIt->second;
        auto blockHash = opinion.m_blockHash;

        if (channel.m_downloadOpinionMap.find(opinion.m_blockHash) == channel.m_downloadOpinionMap.end())
        {
            channel.m_downloadOpinionMap.emplace( std::make_pair(blockHash, DownloadOpinionMapValue
                    (
                            opinion.m_blockHash,
                            opinion.m_downloadChannelId,
                            {}
                     )));
        }

        auto &opinionInfo = channel.m_downloadOpinionMap[blockHash];
        auto &opinions = opinionInfo.m_opinions;
        opinions[opinion.m_opinions[0].m_replicatorKey] = opinion.m_opinions[0];

        // check opinion number
        //_LOG( "///// " << opinionInfo.m_opinions.size() << " " <<  (opinionInfo.m_replicatorNumber*2)/3 );
#ifndef MINI_SIGNATURE
        if (opinions.size() > (channel.m_replicatorsList2.size() * 2) / 3)
#else
        if (opinions.size() >= (channel.m_replicatorsList2.size() * 2) / 3)
#endif
        {
            // start timer if it is not started
            if (!opinionInfo.m_timer)
            {
                //todo check
                opinionInfo.m_timer = m_session->startTimer(m_downloadApprovalTransactionTimerDelayMs,
                                                            [this, &opinionInfo]()
                                                            { onDownloadApprovalTimeExpired(opinionInfo); });
            }
        }
    }
    
    void onDownloadApprovalTimeExpired( DownloadOpinionMapValue& mapValue )
    {
        DBG_MAIN_THREAD
        
        if ( mapValue.m_modifyApproveTransactionSent || mapValue.m_approveTransactionReceived )
            return;

        // notify
        std::vector<DownloadOpinion> opinions;
        for (const auto& [replicatorId, opinion]: mapValue.m_opinions)
        {
            opinions.push_back(opinion);
        }
        auto transactionInfo = DownloadApprovalTransactionInfo{mapValue.m_eventHash, mapValue.m_downloadChannelId, std::move(opinions)};
        m_eventHandler.downloadApprovalTransactionIsReady( *this, transactionInfo );
        mapValue.m_modifyApproveTransactionSent = true;
    }
    
    virtual void asyncInitiateDownloadApprovalTransactionInfo( Hash256 blockHash, Hash256 channelId ) override
    {
        //todo make queue for several simultaneous requests of the same channelId

       boost::asio::post(m_session->lt_session().get_context(), [=,this]() mutable {
        
            DBG_MAIN_THREAD

            if ( m_replicatorIsDestructing )
            {
                return;
            }

            doInitiateDownloadApprovalTransactionInfo( blockHash, channelId );
        });//post
    }

    void doInitiateDownloadApprovalTransactionInfo( const Hash256& blockHash, const Hash256& channelId )
    {
        DBG_MAIN_THREAD
        
        //todo make queue for several simultaneous requests of the same channelId
        
        if ( auto it = m_downloadChannelMap.find( channelId.array() ); it != m_downloadChannelMap.end() )
        {
            //
            // Create my opinion
            //
            
            auto myOpinion = createMyOpinion(it->second);
            
            myOpinion.Sign( keyPair(), blockHash.array(), channelId.array() );
            
            DownloadApprovalTransactionInfo transactionInfo{  blockHash.array(),
                                                            channelId.array(),
                                                            { myOpinion }};
            
            addOpinion( std::move(transactionInfo) );
            shareDownloadOpinion( channelId, blockHash );
        }
        else
        {
            _LOG_ERR( "channelId not found" );
        }
    }

    void shareDownloadOpinion( const Hash256& downloadChannel, const Hash256& eventHash )
    {
        DBG_MAIN_THREAD

        auto it = m_downloadChannelMap.find( downloadChannel.array() );
        _ASSERT( it != m_downloadChannelMap.end() );

        auto eventIt = it->second.m_downloadOpinionMap.find( eventHash.array() );
        _ASSERT( eventIt !=  it->second.m_downloadOpinionMap.end() )

        auto myOpinion = eventIt->second.m_opinions.find(publicKey());
        _ASSERT( myOpinion != eventIt->second.m_opinions.end() );

        DownloadApprovalTransactionInfo opinionToShare = { eventHash.array(), downloadChannel.array(), { myOpinion->second } };

        // send opinion to other Replicators
        std::ostringstream os( std::ios::binary );
        cereal::PortableBinaryOutputArchive archive( os );
        archive( opinionToShare );

        for( const auto& replicatorIt : it->second.m_replicatorsList2 )
        {
            if ( replicatorIt != publicKey() )
            {
                //_LOG( "replicatorIt.m_endpoint: " << replicatorIt.m_endpoint << " " << os.str().length() << " " << dbgReplicatorName() );
                sendMessage( "dn_opinion", replicatorIt.array(), os.str() );
            }
        }

        // Repeat opinion sharing
        eventIt->second.m_opinionShareTimer = m_session->startTimer(m_shareMyDownloadOpinionTimerDelayMs, [=, this]
        {
            shareDownloadOpinion(downloadChannel, eventHash);
        });
    };

    // It is called when drive is closing
    virtual void closeDriveChannels( const mobj<Hash256>& blockHash, const Key& driveKey ) override
    {
        DBG_MAIN_THREAD

        bool deleteDriveImmediately = true;
        
        std::erase_if( m_downloadChannelMap, [](const auto& channelInfo )
        {
            return channelInfo.second.m_isModifyTx;
        });

#ifndef CHANNELS_NOT_OWNED_BY_DRIVES
        if ( blockHash )
        {
            for( auto& [channelId,channelInfo] : m_downloadChannelMap )
            {
                if ( channelInfo.m_driveKey == drive.drivePublicKey().array() && !channelInfo.m_isModifyTx )
                {
                    doInitiateDownloadApprovalTransactionInfo( *blockHash, channelId );

                    // drive will be deleted in 'asyncDownloadApprovalTransactionHasBeenPublished()'
                    deleteDriveImmediately = false;
                }
            }
        }
#endif

        _LOG( "deleteDriveImmediately" )

        if ( deleteDriveImmediately )
        {
            deleteDrive( driveKey.array() );
        }
    }

    void asyncDownloadApprovalTransactionHasFailedInvalidOpinions( Hash256 eventHash, Hash256 channelId ) override
    {
       boost::asio::post(m_session->lt_session().get_context(), [=,this]() mutable {

            DBG_MAIN_THREAD

            if ( m_replicatorIsDestructing )
            {
                return;
            }

            if ( auto channelIt = m_downloadChannelMap.find( channelId.array() ); channelIt != m_downloadChannelMap.end())
            {
                if ( channelIt->second.m_isClosed )
                {
                    return;
                }

                auto& opinions = channelIt->second.m_downloadOpinionMap;
                if ( auto opinionInfoIt = opinions.find( eventHash.array() ); opinionInfoIt != opinions.end() )
                {
                    auto& opinionInfo = opinionInfoIt->second;
                    if ( opinionInfo.m_approveTransactionReceived )
                    {
                        return;
                    }
                    if ( opinionInfo.m_timer )
                    {
                        opinionInfo.m_timer.reset();
                    }
                    auto receivedOpinions = opinionInfo.m_opinions;
                    opinionInfo.m_opinions.clear();
                    opinionInfo.m_modifyApproveTransactionSent=false;
                    for (const auto& [key, opinion]: receivedOpinions)
                    {
                        processDownloadOpinion(DownloadApprovalTransactionInfo
                        {
                            opinionInfo.m_eventHash,
                            opinionInfo.m_downloadChannelId,
                            {opinion}
                        });
                    }
                }
                else
                {
                    _LOG_ERR( "eventHash not found" );
                }
            }
            else {
                _LOG_ERR( "channelId not found" );
            }
        });//post
    }
    
    virtual void asyncDownloadApprovalTransactionHasBeenPublished( Hash256 eventHash, Hash256 channelId, bool driveIsClosed ) override
    {
       boost::asio::post(m_session->lt_session().get_context(), [=,this]() mutable {
        
            DBG_MAIN_THREAD

            if ( m_replicatorIsDestructing )
            {
                return;
            }

            // clear opinion map
            if ( auto channelIt = m_downloadChannelMap.find( channelId.array() ); channelIt != m_downloadChannelMap.end())
            {
                auto& opinions = channelIt->second.m_downloadOpinionMap;
                if ( auto it = opinions.find( eventHash.array() ); it != opinions.end() )
                {
                    it->second.m_timer.reset();
                    it->second.m_opinionShareTimer.reset();
                    it->second.m_approveTransactionReceived = true;
                }
                else
                {
                    _LOG_ERR( "eventHash not found" );
                }
            }
            else
            {
                _LOG_ERR( "channelId not found" );
            }

#ifndef CHANNELS_NOT_OWNED_BY_DRIVES
            if ( !driveIsClosed )
            {
                return;
            }

            // Is it happened while drive is closing?
            if ( auto channelIt = m_downloadChannelMap.find( channelId.array() ); channelIt != m_downloadChannelMap.end() )
            {
                const auto& driveKey = channelIt->second.m_driveKey;

                if ( auto drive = getDrive( driveKey ); drive )
                {
                    bool driveWillBeDeleted = false;

                    if ( drive->isItClosingTxHash( eventHash ) )
                    {
                        channelIt->second.m_isClosed = true;

                        // TODO Potential performance bottleneck
                        driveWillBeDeleted = std::find_if(m_downloadChannelMap.begin(), m_downloadChannelMap.end(),[&driveKey] (const auto& value)
                              {
                                  return value.second.m_driveKey == driveKey && !value.second.m_isClosed;
                              }) == m_downloadChannelMap.end();
                    }

                    if ( driveWillBeDeleted )
                    {
                        deleteDrive( driveKey );
                    }
                }

            }
#endif
        });//post
    }

    void deleteDrive( const std::array<uint8_t,32>& driveKey )
    {
        DBG_MAIN_THREAD

#ifndef CHANNELS_NOT_OWNED_BY_DRIVES
        std::erase_if( m_downloadChannelMap, [&driveKey] (const auto& item) {
            return item.second.m_driveKey == driveKey;
        });
#endif

        std::erase_if( m_modifyDriveMap, [&driveKey] (const auto& item) {
            return item.second.m_driveKey == driveKey;
        });
    }

    void finishDriveClosure ( const Key& driveKey ) override
    {
        DBG_MAIN_THREAD

        m_driveMap.erase( driveKey );
    }
    
    virtual void asyncOnOpinionReceived( ApprovalTransactionInfo anOpinion ) override
    {
       boost::asio::post(m_session->lt_session().get_context(), [=,this]() mutable {

            DBG_MAIN_THREAD

            if ( m_replicatorIsDestructing )
            {
                return;
            }

            if ( auto drive = getDrive( anOpinion.m_driveKey ); drive )
            {
                drive->onOpinionReceived( anOpinion );
            }
            else
            {
                _LOG_ERR( "drive not found" );
            }
        });
    }


    void processOpinion( const ApprovalTransactionInfo& anOpinion ) override
    {
        DBG_MAIN_THREAD
        
        m_eventHandler.opinionHasBeenReceived(*this, anOpinion);
    }
    
    virtual void asyncApprovalTransactionHasBeenPublished( PublishedModificationApprovalTransactionInfo transaction ) override
    {
       boost::asio::post(m_session->lt_session().get_context(), [=,this]() mutable {

            DBG_MAIN_THREAD

            if ( m_replicatorIsDestructing )
            {
                return;
            }

            if ( auto drive = getDrive( transaction.m_driveKey ); drive )
            {
                //(???)
                addModifyDriveInfo( transaction.m_modifyTransactionHash,
                                    transaction.m_driveKey,
                                    LONG_LONG_MAX,
                                    drive->getClient(),
                                    drive->getReplicators());

                drive->onApprovalTransactionHasBeenPublished( transaction );
            }
            else
            {
                _LOG_ERR( "drive not found" );
            }
        });//post
    }

    void asyncApprovalTransactionHasFailedInvalidSignatures(Key driveKey, Hash256 transactionHash) override
    {
       boost::asio::post(m_session->lt_session().get_context(), [=,this]() mutable {

            DBG_MAIN_THREAD

            if ( m_replicatorIsDestructing )
            {
                return;
            }

            if ( auto drive = getDrive( driveKey ); drive )
            {
                drive->onApprovalTransactionHasFailedInvalidOpinions( transactionHash );
            }
            else
            {
                _LOG_ERR( "drive not found" );
            }
        });//post
    }
    
    virtual void asyncSingleApprovalTransactionHasBeenPublished( PublishedModificationSingleApprovalTransactionInfo transaction ) override
    {
       boost::asio::post(m_session->lt_session().get_context(), [=,this]() mutable {
        
            DBG_MAIN_THREAD

            if ( m_replicatorIsDestructing )
            {
                return;
            }

            if ( auto drive = getDrive( transaction.m_driveKey ); drive )
            {
                drive->onSingleApprovalTransactionHasBeenPublished( transaction );
            }
            else
            {
                _LOG_ERR( "drive not found" );
            }
        });//post
    }

    virtual void asyncVerifyApprovalTransactionHasBeenPublished( PublishedVerificationApprovalTransactionInfo info ) override
    {
        boost::asio::post(m_session->lt_session().get_context(), [=,this]() mutable {

            DBG_MAIN_THREAD

            if ( m_replicatorIsDestructing )
            {
                return;
            }

            if ( auto drive = getDrive( info.m_driveKey ); drive )
            {
                drive->onVerifyApprovalTransactionHasBeenPublished( info );
            }
            else
            {
                _LOG_ERR( "drive not found" );
            }
        });//post
    }

    void asyncVerifyApprovalTransactionHasFailedInvalidOpinions( Key driveKey, Hash256 verificationId ) override
    {}

    virtual void sendMessage( const std::string&             query,
                              const std::array<uint8_t,32>&  replicatorKey,
                              const std::string&             message ) override
    {
        DBG_MAIN_THREAD

        if ( m_replicatorIsDestructing )
        {
            return;
        }

        auto endpointTo = m_endpointsManager.getEndpoint( replicatorKey );
        if ( endpointTo )
        {
            m_session->sendMessage( query, { endpointTo->address(), endpointTo->port() }, message );
        }
    }

    virtual void sendMessage( const std::string&                      query,
                              const std::array<uint8_t,32>&           replicatorKey,
                              const std::vector<uint8_t>&             message ) override
    {
        DBG_MAIN_THREAD

        if ( m_replicatorIsDestructing )
        {
            return;
        }

        auto endpointTo = m_endpointsManager.getEndpoint( replicatorKey );
        if ( endpointTo )
        {
            __LOG( "*** sendMessage: " << query << " to: " << *endpointTo << " " << int(replicatorKey[0]) );
            m_session->sendMessage( query, { endpointTo->address(), endpointTo->port() }, message );
        }
        else
        {
            __LOG( "sendMessage: absent: " << int(replicatorKey[0]) );
        }
    }

    virtual void onMessageReceived( const std::string& query,
                                    const std::string& message,
                                    const boost::asio::ip::udp::endpoint& source ) override
    {
        try {

        DBG_MAIN_THREAD

        //todo
        if ( query == "opinion" )
        {
            std::istringstream is( message, std::ios::binary );
            cereal::PortableBinaryInputArchive iarchive(is);
            ApprovalTransactionInfo info;
            iarchive( info );

            processOpinion(info);
            return;
        }
        else if ( query == "dn_opinion" )
        {
            std::istringstream is( message, std::ios::binary );
            cereal::PortableBinaryInputArchive iarchive(is);
            DownloadApprovalTransactionInfo info;
            iarchive( info );

            processDownloadOpinion(info);
            return;
        }
        else if ( query == "code_verify" )
        {
            std::istringstream is( message, std::ios::binary );
            cereal::PortableBinaryInputArchive iarchive(is);

            mobj<VerificationCodeInfo> info{VerificationCodeInfo{}};
            iarchive( *info );
            processVerificationCode( std::move(info) );
            return;
        }

        else if ( query == "verify_opinion" )
        {
            std::istringstream is( message, std::ios::binary );
            cereal::PortableBinaryInputArchive iarchive(is);
            
            mobj<VerifyApprovalTxInfo> info{VerifyApprovalTxInfo{}};
            iarchive( *info );
            processVerificationOpinion( std::move(info) );
            return;
        }
        else if ( query == "handshake" )
        {
            std::istringstream is( message, std::ios::binary );
            cereal::PortableBinaryInputArchive iarchive(is);
            DhtHandshake handshake;
            iarchive( handshake );

            processHandshake( handshake, {source.address(), source.port()} );
            return;
        }
        else if ( query == "endpoint_request" ) {
            std::istringstream is( message, std::ios::binary );
            cereal::PortableBinaryInputArchive iarchive(is);
            ExternalEndpointRequest request;
            iarchive( request );

            processEndpointRequest(request, {source.address(), source.port()});
            return;
        }
        else if ( query == "endpoint_response" ) {
            std::istringstream is( message, std::ios::binary );
            cereal::PortableBinaryInputArchive iarchive(is);
            ExternalEndpointResponse response;
            iarchive( response );

            m_endpointsManager.updateExternalEndpoint(response);
            return;
        }

        _ASSERT(0);

        } catch(...)
        {
            _LOG_ERR( "onMessageReceived: invalid message format: query=" << query );
        }
    }

    void onSyncDnOpinionReceived( const std::string& retString ) override
    {
        try
        {
            // parse response
            //
            std::istringstream is( retString, std::ios::binary );
            cereal::PortableBinaryInputArchive iarchive(is);
            uint8_t hasResponse;
            iarchive( hasResponse );
            if ( !hasResponse )
            {
                // no opinion
                return;
            }

            std::array<uint8_t,32> driveKey;
            std::array<uint8_t,32> channelHash;
            mobj<DownloadOpinion> opinion{DownloadOpinion{}};
            iarchive( driveKey );
            iarchive( channelHash );
            iarchive( *opinion );

            if ( !opinion->Verify( driveKey, channelHash ) )
            {
                _LOG_WARN( "invalid download sync opinion from " << Key(opinion->m_replicatorKey) )
            }

            m_dnOpinionSyncronizer.addSyncOpinion( channelHash, std::move(opinion), m_downloadChannelMap );
        }
        catch(...)
        {
            _LOG_WARN( "invalid 'get_dn_rcpts' response" )
            return;
        }
    }

    void processVerificationCode( mobj<VerificationCodeInfo>&& info )
    {
        DBG_MAIN_THREAD
        
        if ( !info->Verify() )
        {
            _LOG_WARN("processVerificationCode: bad sign: " << Hash256(info->m_tx) )
            return;
        }

        if ( auto driveIt = m_driveMap.find( info->m_driveKey ); driveIt != m_driveMap.end() )
        {
            driveIt->second->onVerificationCodeReceived( std::move(info) );
            return;
        }

        _LOG_WARN( "processVerificationCode: unknown drive: " << Key(info->m_driveKey) );
    }

    void processVerificationOpinion( mobj<VerifyApprovalTxInfo>&& info )
    {
        DBG_MAIN_THREAD

        if ( info->m_opinions.size() != 1 )
        {
            _LOG_WARN("processVerificationOpinion: invalid opinion size: " << info->m_opinions.size() )
            return;
        }

        if ( !info->m_opinions[0].Verify( info->m_tx, info->m_driveKey, info->m_shardId ) )
        {
            _LOG_WARN("processVerificationOpinion: bad sign: " << Key(info->m_opinions[0].m_publicKey) )
            return;
        }

        if ( auto driveIt = m_driveMap.find( info->m_driveKey ); driveIt != m_driveMap.end() )
        {
            driveIt->second->onVerificationOpinionReceived( std::move(info) );
            return;
        }

        _LOG_WARN( "processVerificationCode: unknown drive: " << Key(info->m_driveKey) );
    }

    void        setDownloadApprovalTransactionTimerDelay( int miliseconds ) override
    {
        m_downloadApprovalTransactionTimerDelayMs = miliseconds;
    }

    void        setModifyApprovalTransactionTimerDelay( int miliseconds ) override
    {
        m_modifyApprovalTransactionTimerDelayMs = miliseconds;
    }
    
    int         getModifyApprovalTransactionTimerDelay() override
    {
        return m_modifyApprovalTransactionTimerDelayMs;
    }

    void        setVerifyCodeTimerDelay( int miliseconds ) override
    {
        m_verifyCodeTimerDelayMs = miliseconds;
    }

    int         getVerifyCodeTimerDelay() override
    {
        return m_verifyCodeTimerDelayMs;
    }

    void        setVerifyApprovalTransactionTimerDelay( int miliseconds ) override
    {
        m_verifyApprovalTransactionTimerDelayMs = miliseconds;
    }

    int         getVerifyApprovalTransactionTimerDelay() override
    {
        return m_verifyApprovalTransactionTimerDelayMs;
    }

    void        setSessionSettings(const lt::settings_pack& settings, bool localNodes) override
    {
        m_session->lt_session().apply_settings(settings);
        if (localNodes) {
            std::uint32_t const mask = 1 << lt::session::global_peer_class_id;
            lt::ip_filter f;
            f.add_rule(lt::make_address("0.0.0.0"), lt::make_address("255.255.255.255"), mask);
            m_session->lt_session().set_peer_class_filter(f);
        }
    }

    const char* dbgReplicatorName() const override { return m_dbgOurPeerName.c_str(); }
    
    virtual std::shared_ptr<sirius::drive::FlatDrive> dbgGetDrive( const std::array<uint8_t,32>& driveKey ) override
    {
        if ( auto it = m_driveMap.find(driveKey); it != m_driveMap.end() )
        {
            return it->second;
        }
        assert(0);
    }
    
    void saveDownloadChannelMap()
    {
        std::ostringstream os( std::ios::binary );
        cereal::PortableBinaryOutputArchive archive( os );
        archive( m_downloadChannelMap );

        saveRestartData( fs::path(m_storageDirectory) / "downloadChannelMap", os.str() );
    }
    
    bool loadDownloadChannelMap()
    {
        std::string data;
        
        if ( !loadRestartData( fs::path(m_storageDirectory) / "downloadChannelMap", data ) )
        {
            return false;
        }
        
        std::istringstream is( data, std::ios::binary );
        cereal::PortableBinaryInputArchive iarchive(is);
        iarchive( m_downloadChannelMapBackup );
        return true;
    }
    


private:
    std::shared_ptr<sirius::drive::Session> session() {
        return m_session;
    }
};

std::shared_ptr<Replicator> createDefaultReplicator(
                                        const crypto::KeyPair& keyPair,
                                        std::string&&       address,
                                        std::string&&       port,
                                        std::string&&       storageDirectory,
                                        std::string&&       sandboxDirectory,
                                        const std::vector<ReplicatorInfo>&  bootstraps,
                                        bool                                useTcpSocket,
                                        ReplicatorEventHandler&             handler,
                                        DbgReplicatorEventHandler*          dbgEventHandler,
                                        const char*                         dbgReplicatorName )
{
    return std::make_shared<DefaultReplicator>(
                                               keyPair,
                                               std::move(address),
                                               std::move(port),
                                               std::move(storageDirectory),
                                               std::move(sandboxDirectory),
                                               useTcpSocket,
                                               handler,
                                               dbgEventHandler,
                                               bootstraps,
                                               dbgReplicatorName );
}

}

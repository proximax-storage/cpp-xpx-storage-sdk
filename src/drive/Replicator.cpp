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

#include <cereal/types/vector.hpp>
#include <cereal/types/array.hpp>
#include <cereal/types/map.hpp>
#include <cereal/archives/portable_binary.hpp>

#include <libtorrent/alert_types.hpp>

#include <filesystem>
#include <mutex>
#include <future>

namespace sirius::drive {

#define USE_OUR_IO_CONTEXT

//
// DefaultReplicator
//
class DefaultReplicator : public DownloadLimiter // Replicator
{
private:
#ifdef USE_OUR_IO_CONTEXT
    boost::asio::io_context m_replicatorContext;
    std::thread             m_libtorrentThread;
#endif
    
    // Session listen interface
    std::string m_address;
    std::string m_port;

    // Folders for drives and sandboxes
    std::string m_storageDirectory;
    std::string m_sandboxDirectory;
    
    int         m_downloadApprovalTransactionTimerDelayMs = 60*1000;
    int         m_modifyApprovalTransactionTimerDelayMs   = 60*1000;
    std::mutex  m_replicatorDestructingMutex;
    bool        m_replicatorIsDestructing = false;

    bool        m_useTcpSocket;
    
    ReplicatorEventHandler& m_eventHandler;
    DbgReplicatorEventHandler*  m_dbgEventHandler;
    
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
               const char*   dbgReplicatorName ) : DownloadLimiter( keyPair, dbgReplicatorName ),

        m_address( std::move(address) ),
        m_port( std::move(port) ),
        m_storageDirectory( std::move(storageDirectory) ),
        m_sandboxDirectory( std::move(sandboxDirectory) ),
        m_useTcpSocket( useTcpSocket ),
        m_eventHandler( handler ),
        m_dbgEventHandler( dbgEventHandler )
    {
    }

    virtual ~DefaultReplicator()
    {
#ifdef DEBUG_OFF_CATAPULT
        _LOG( "~DefaultReplicator() ")
#endif
        
        m_replicatorDestructingMutex.lock();
        m_replicatorIsDestructing = true;
        m_replicatorDestructingMutex.unlock();

        std::unique_lock<std::shared_mutex> lock(m_driveMutex);

        for( auto& [key,drive]: m_driveMap )
        {
            drive->terminate();
        }

        m_session->lt_session().get_context().post( [this]() mutable {
            m_downloadChannelMap.clear();
            m_modifyDriveMap.clear();
            m_driveMap.clear();
        });
        
        m_session->endSession();
        
        {
            auto blockedDestructor = m_session->lt_session().abort();
            m_session.reset();

#ifdef USE_OUR_IO_CONTEXT
        if ( m_libtorrentThread.joinable() )
        {
            m_libtorrentThread.join();
        }
#endif
        saveDownloadChannelMap();
        }
    }
    
    void start() override
    {
#ifdef USE_OUR_IO_CONTEXT
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
                                         m_useTcpSocket );
#else
        m_session = createDefaultSession( m_address + ":" + m_port, [port=m_port,this] (const lt::alert* pAlert)
                                         {
                                             if ( pAlert->type() == lt::listen_failed_alert::alert_type )
                                             {
                                                 _LOG_WARN( "Replicator session alert: " << pAlert->message() );
                                                 _LOG_WARN( "Port is busy?: " << port );
                                                 m_eventHandler.onLibtorrentSessionError( pAlert->message() );
                                             }
                                         },
                                         weak_from_this(),
                                         m_useTcpSocket );
#endif
        m_session->lt_session().m_dbgOurPeerName = m_dbgOurPeerName.c_str();
        
#ifdef USE_OUR_IO_CONTEXT
        m_libtorrentThread = std::thread( [this] {
            m_replicatorContext.run();
#ifdef DEBUG_OFF_CATAPULT
            _LOG( "libtorrentThread ended" );
#endif
        });
        m_dbgThreadId = m_libtorrentThread.get_id();
#else
        std::mutex waitMutex;
        waitMutex.lock();
        m_session->lt_session().get_context().post( [=,&waitMutex,this]() mutable {
            m_dbgThreadId = std::this_thread::get_id();
            waitMutex.unlock();
        });//post
        waitMutex.lock();
#endif

        removeDriveDataOfBrokenClose();
        loadDownloadChannelMap();
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
        if ( const auto drive = getDrive(driveKey); drive )
        {
            auto rootHash = drive->rootHash();
            LOG( "getRootHash of: " << driveKey << " -> " << rootHash );
            return rootHash;
        }

        _LOG_ERR( "unknown drive: " << driveKey );
        throw std::runtime_error( std::string("unknown dive: ") + toString(driveKey.array()) );

        return Hash256();
    }

    void printDriveStatus( const Key& driveKey ) override
    {
        if ( const auto drive = getDrive(driveKey); drive )
        {
            return drive->printDriveStatus();
        }

        _LOG_ERR( "unknown dive: " << driveKey );
        throw std::runtime_error( std::string("unknown dive: ") + toString(driveKey.array()) );

    }

    // 'actualRootHash' is not used
    //
    void asyncAddDrive( Key driveKey, AddDriveRequest driveRequest) override
    {
        m_session->lt_session().get_context().post( [=,this]() mutable {
        
            DBG_MAIN_THREAD
            
            if ( m_replicatorDestructingMutex.try_lock() && !m_replicatorIsDestructing )
            {
                m_replicatorDestructingMutex.unlock();

                _LOG( "adding drive " << driveKey );

                std::unique_lock<std::shared_mutex> lock(m_driveMutex);
                
                if (m_driveMap.find(driveKey) != m_driveMap.end()) {
                    _LOG( "drive already added" );
                    return;
                }

                // Exclude itself from replicator list
                for( auto it = driveRequest.replicators.begin();  it != driveRequest.replicators.end(); it++ )
                {
                    if ( it->m_publicKey == publicKey() )
                    {
                        driveRequest.replicators.erase( it );
                        break;
                    }
                }

                auto drive = sirius::drive::createDefaultFlatDrive(
                        session(),
                        m_storageDirectory,
                        m_sandboxDirectory,
                        driveKey,
                        driveRequest.client,
                        driveRequest.driveSize,
                        driveRequest.expectedCumulativeDownloadSize,
                        m_eventHandler,
                        *this,
                        driveRequest.replicators,
                        m_dbgEventHandler );

                m_driveMap[driveKey] = drive;

//            if ( actualRootHash && drive->rootHash() != actualRootHash )
//            {
//                drive->startCatchingUp( CatchingUpRequest{ *actualRootHash, {} } );
//            }

            // Notify
//            if ( m_dbgEventHandler ) {
//                m_dbgEventHandler->driveAdded(drive->drivePublicKey());
//            }
            }
        });//post
    }

    void asyncCloseDrive( Key driveKey, Hash256 transactionHash ) override
    {
        m_session->lt_session().get_context().post( [=,this]() mutable {
        
            DBG_MAIN_THREAD

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

    void asyncModify( Key driveKey, ModifyRequest modifyRequest ) override
    {
        m_session->lt_session().get_context().post( [=,this]() mutable {
        
            DBG_MAIN_THREAD

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
                                modifyRequest.m_replicatorList );

            for( auto it = modifyRequest.m_replicatorList.begin();  it != modifyRequest.m_replicatorList.end(); it++ )
            {
                if ( it->m_publicKey == publicKey() )
                {
                    modifyRequest.m_replicatorList.erase( it );
                    break;
                }
            }

            pDrive->startModifyDrive( std::move(modifyRequest) );
        });//post
    }
    
    void asyncCancelModify( Key driveKey, Hash256 transactionHash ) override
    {
        m_session->lt_session().get_context().post( [=,this]() mutable {
        
            DBG_MAIN_THREAD

            if ( const auto drive = getDrive(driveKey); drive )
            {
                drive->cancelModifyDrive( transactionHash );
                return;
            }

            _LOG( "cancelModify: unknown drive: " << driveKey );
        });//post
    }
    
    void asyncStartDriveVerification( Key driveKey, VerificationRequest&& request ) override
    {
        m_session->lt_session().get_context().post( [=,this]() mutable {
        
            DBG_MAIN_THREAD

            if ( const auto drive = getDrive(driveKey); drive )
            {
                drive->startDriveVerification( std::move(request) );
                return;
            }

            _LOG( "cancelModify: unknown drive: " << driveKey );
        });//post
    }

    
    void asyncAddDownloadChannelInfo( Key driveKey, DownloadRequest&& request ) override
    {
        m_session->lt_session().get_context().post( [=,this]() mutable {
        
            DBG_MAIN_THREAD

            std::vector<std::array<uint8_t,32>> clientList;
            for( const auto& it : request.m_clients )
                clientList.push_back( it.array() );
            addChannelInfo( request.m_channelKey.array(),
                            request.m_prepaidDownloadSize,
                            driveKey,
                            request.m_addrList,
                            clientList);
        });//post
    }

    void removeDownloadChannelInfo( const std::array<uint8_t,32>& channelKey ) override
    {
        m_session->lt_session().get_context().post( [=,this]() mutable {

            DBG_MAIN_THREAD
            
            removeChannelInfo(channelKey);
        });
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
                if ( replicatorIt->m_publicKey != replicatorPublicKey )
                {
                    //_LOG( "todo++++ sendMessage(rcpt) " << m_dbgOurPeerName << " " << int(downloadChannelId[0]) );
                    m_session->sendMessage( "rcpt", { replicatorIt->m_endpoint.address(), replicatorIt->m_endpoint.port() }, message );
                }
            }
        }
    }

    virtual void asyncOnDownloadOpinionReceived( DownloadApprovalTransactionInfo anOpinion ) override
    {
        m_session->lt_session().get_context().post( [=,this]() mutable {

            DBG_MAIN_THREAD

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
            if ( auto downloadedIt = info.m_replicatorUploadMap.find( replicatorIt.m_publicKey.array()); downloadedIt != info.m_replicatorUploadMap.end() )
            {
                myOpinion.m_downloadLayout.push_back( {downloadedIt->first, downloadedIt->second.m_uploadedSize} );
            }
            else if ( replicatorIt.m_publicKey == publicKey() )
            {
                myOpinion.m_downloadLayout.push_back( { publicKey(), info.m_uploadedSize } );
            }
            else
            {
                myOpinion.m_downloadLayout.push_back( { replicatorIt.m_publicKey.array(), 0 } );
            }
        }
        
        return myOpinion;
    }

    void addOpinion(DownloadApprovalTransactionInfo &&opinion)
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
                return (now - value.m_creationTime).seconds() > 60 * 60;
            });
        }

        //
        // add opinion
        //
        auto channelIt = m_downloadChannelMap.find(opinion.m_downloadChannelId);

        if (channelIt == m_downloadChannelMap.end())
        {
            _LOG_ERR("Attempt to add opinion for a non-existing channel");
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
        //todo not ">=..."!!! - "> (opinionInfo.m_replicatorNumber*2)/3
        if (opinions.size() >= (channel.m_replicatorsList2.size() * 2) / 3)
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

        m_session->lt_session().get_context().post( [=,this]() mutable {
        
            DBG_MAIN_THREAD
            doInitiateDownloadApprovalTransactionInfo( blockHash, channelId );
        });//post
    }

    void doInitiateDownloadApprovalTransactionInfo( Hash256 blockHash, Hash256 channelId )
    {
        DBG_MAIN_THREAD
        
        //todo make queue for several simultaneous requests of the same channelId
        
        if ( auto it = m_downloadChannelMap.find( channelId.array() ); it != m_downloadChannelMap.end() )
        {
            const auto& replicatorsList = it->second.m_replicatorsList2;

            //
            // Create my opinion
            //
            
            auto myOpinion = createMyOpinion(it->second);
            
            myOpinion.Sign( keyPair(), blockHash.array(), channelId.array() );
            
            DownloadApprovalTransactionInfo transactionInfo{  blockHash.array(),
                                                            channelId.array(),
                                                            { myOpinion }};
            
            //
            // Send my opinion to other replicators
            //

            std::ostringstream os( std::ios::binary );
            cereal::PortableBinaryOutputArchive archive( os );
            archive( transactionInfo );

            for( const auto& replicatorIt : replicatorsList )
            {
                if ( replicatorIt.m_publicKey != publicKey() )
                {
                    //_LOG( "replicatorIt.m_endpoint: " << replicatorIt.m_endpoint << " " << os.str().length() << " " << dbgReplicatorName() );
                    sendMessage( "dnopinion", replicatorIt.m_endpoint, os.str() );
                }
            }

            addOpinion( std::move(transactionInfo) );
        }
        else
        {
            _LOG_ERR( "channelId not found" );
        }
    }
    
    // It is called when drive is closing
    virtual void closeDriveChannels( const Hash256& blockHash, FlatDrive& drive ) override
    {
        DBG_MAIN_THREAD

        bool deleteDriveImmediately = true;
        
        std::erase_if( m_downloadChannelMap, [](const auto& channelInfo )
        {
            return channelInfo.second.m_isModifyTx;
        });

#ifndef CHANNELS_NOT_OWNED_BY_DRIVES
        for( auto& [channelId,channelInfo] : m_downloadChannelMap )
        {
            if ( channelInfo.m_driveKey == drive.drivePublicKey().array() && !channelInfo.m_isModifyTx )
            {
                doInitiateDownloadApprovalTransactionInfo( blockHash, channelId );
                
                // drive will be deleted in 'asyncDownloadApprovalTransactionHasBeenPublished()'
                deleteDriveImmediately = false;
            }
        }
#endif
        
        if ( deleteDriveImmediately )
        {
            deleteDrive( drive.drivePublicKey().array() );
        }
    }

    void asyncDownloadApprovalTransactionHasFailedInvalidOpinions( Hash256 eventHash, Hash256 channelId ) override
    {
        m_session->lt_session().get_context().post( [=,this]() mutable {

            DBG_MAIN_THREAD

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
        m_session->lt_session().get_context().post( [=,this]() mutable {
        
            DBG_MAIN_THREAD
            
            // clear opinion map
            if ( auto channelIt = m_downloadChannelMap.find( channelId.array() ); channelIt != m_downloadChannelMap.end())
            {
                auto& opinions = channelIt->second.m_downloadOpinionMap;
                if ( auto it = opinions.find( eventHash.array() ); it != opinions.end() )
                {
                    if ( it->second.m_timer )
                    {
                        it->second.m_timer.reset();
                    }
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

                    if ( drive->closingTxHash() == eventHash )
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

        std::unique_lock<std::shared_mutex> lock(m_driveMutex);

        auto driveIt = m_driveMap.find( driveKey );
        assert( driveIt != m_driveMap.end() );

        driveIt->second->removeAllDriveData();
        
        m_driveMap.erase( driveIt );
    }
    
    virtual void asyncOnOpinionReceived( ApprovalTransactionInfo anOpinion ) override
    {
        m_session->lt_session().get_context().post( [=,this]() mutable {

            DBG_MAIN_THREAD

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
        m_session->lt_session().get_context().post( [=,this]() mutable {

            DBG_MAIN_THREAD

            if ( auto drive = getDrive( transaction.m_driveKey ); drive )
            {
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
        m_session->lt_session().get_context().post( [=,this]() mutable {

            DBG_MAIN_THREAD

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
        m_session->lt_session().get_context().post( [=,this]() mutable {
        
            DBG_MAIN_THREAD

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
    
    virtual void sendMessage( const std::string& query, boost::asio::ip::tcp::endpoint endpoint, const std::string& message ) override
    {
        DBG_MAIN_THREAD
        
        m_session->sendMessage( query, { endpoint.address(), endpoint.port() }, message );
    }
    
    virtual void sendMessage( const std::string&             query,
                              const std::array<uint8_t,32>&  replicatorKey,
                              const std::string&             message ) override
    {
        //TODO
    }

    virtual void onMessageReceived( const std::string& query, const std::string& message ) override try
    {
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
        else if ( query ==  "dnopinion" )
        {
            std::istringstream is( message, std::ios::binary );
            cereal::PortableBinaryInputArchive iarchive(is);
            DownloadApprovalTransactionInfo info;
            iarchive( info );

            processDownloadOpinion(info);
            return;
        }
        
        assert(0);
    }
    catch(...)
    {
        _LOG_ERR( "onMessageReceived: invalid message format: query=" << query );
    }


    ReplicatorEventHandler& eventHandler() override
    {
        return m_eventHandler;
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
        std::shared_lock<std::shared_mutex> lock(m_driveMutex);
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
                                        bool                useTcpSocket,
                                        ReplicatorEventHandler&     handler,
                                        DbgReplicatorEventHandler*  dbgEventHandler,
                                        const char*         dbgReplicatorName )
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
                                               dbgReplicatorName );
}

}

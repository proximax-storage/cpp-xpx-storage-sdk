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
#include <cereal/archives/portable_binary.hpp>

#include <libtorrent/alert_types.hpp>
#include <filesystem>

#include <mutex>

/*#define DBG_SINGLE_THREAD { std::cout << m_dbgThreadId << "==" << std::this_thread::get_id() << std::endl; \
                            assert( m_dbgThreadId == std::this_thread::get_id() ); }*/

namespace sirius::drive {

//
// DefaultReplicator
//
class DefaultReplicator : public DownloadLimiter // Replicator
{
public:
    std::shared_ptr<Session> m_session;

    // Session listen interface
    std::string m_address;
    std::string m_port;

    // Folders for drives and sandboxes
    std::string m_storageDirectory;
    std::string m_sandboxDirectory;
    
    int         m_downloadApprovalTransactionTimerDelayMs = 60*1000;
    int         m_modifyApprovalTransactionTimerDelayMs   = 60*1000;

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

    void start() override
    {
        m_session = createDefaultSession( m_address + ":" + m_port, [port=m_port] (const lt::alert* pAlert)
            {
                if ( pAlert->type() == lt::listen_failed_alert::alert_type ) {
                    LOG( "Replicator session alert: " << pAlert->message() );
                    LOG( "Port is busy?: " << port );
                }
            },
            weak_from_this(),
            m_useTcpSocket );
        m_session->lt_session().m_dbgOurPeerName = m_dbgOurPeerName.c_str();
        
        std::mutex waitMutex;
        waitMutex.lock();
        m_session->lt_session().get_context().post( [=,&waitMutex,this]() mutable {
            m_dbgThreadId = std::this_thread::get_id();
            waitMutex.unlock();
        });//post
        waitMutex.lock();

    }

    Hash256 dbgGetRootHash( const Key& driveKey ) override
    {
        if ( const auto driveIt = m_driveMap.find(driveKey); driveIt != m_driveMap.end() )
        {
            auto rootHash = driveIt->second->rootHash();
            LOG( "getRootHash of: " << driveKey << " -> " << rootHash );
            return rootHash;
        }

        LOG_ERR( "unknown drive: " << driveKey );
        throw std::runtime_error( std::string("unknown dive: ") + toString(driveKey.array()) );

        return Hash256();
    }

    void printDriveStatus( const Key& driveKey ) override
    {
        std::shared_lock<std::shared_mutex> lock(m_driveMutex);
        if ( const auto driveIt = m_driveMap.find(driveKey); driveIt != m_driveMap.end() )
        {
            return driveIt->second->printDriveStatus();
        }

        LOG_ERR( "unknown dive: " << driveKey );
        throw std::runtime_error( std::string("unknown dive: ") + toString(driveKey.array()) );

    }

    // 'actualRootHash' is empty ONLY when 'asyncAddDrive()' creates new drive
    //
    void asyncAddDrive( Key driveKey, AddDriveRequest driveRequest, std::optional<InfoHash> actualRootHash ) override
    {
        m_session->lt_session().get_context().post( [=,this]() mutable {
        
            DBG_SINGLE_THREAD
            
            LOG( "adding drive " << driveKey );

            std::unique_lock<std::shared_mutex> lock(m_driveMutex);

            if (m_driveMap.find(driveKey) != m_driveMap.end()) {
                return;
                _LOG( "drive already added" );
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
                    driveRequest.driveSize,
                    driveRequest.usedDriveSizeExcludingMetafiles,
                    m_eventHandler,
                    *this,
                    driveRequest.replicators,
                    m_dbgEventHandler );

            m_driveMap[driveKey] = drive;

            if ( actualRootHash && drive->rootHash() != actualRootHash )
            {
                drive->startCatchingUp( CatchingUpRequest{ *actualRootHash, {} } );
            }

            // Notify
            if ( m_dbgEventHandler ) {
                m_dbgEventHandler->driveAdded(drive->drivePublicKey());
            }
        });//post
    }

    void asyncCloseDrive( Key driveKey, Hash256 transactionHash ) override
    {
        m_session->lt_session().get_context().post( [=,this]() mutable {
        
            DBG_SINGLE_THREAD

            if ( auto driveIt = m_driveMap.find(driveKey); driveIt != m_driveMap.end() )
            {
                driveIt->second->startDriveClosing( transactionHash );
            }
            else
            {
                _LOG( "removeDrive: drive not found: " << driveKey );
                return;
            }
        });//post
    }

//todo    std::shared_ptr<sirius::drive::FlatDrive> getDrive( const Key& driveKey ) override {
//        LOG( "getDrive " << driveKey );
//
//        std::shared_lock<std::shared_mutex> lock(m_driveMutex);
//
//        if (m_driveMap.find(driveKey) == m_driveMap.end())
//        {
//            LOG( "drive not found " << driveKey );
//            return nullptr;
//        }
//
//        return m_driveMap[driveKey];
//    }

    void asyncModify( Key driveKey, ModifyRequest modifyRequest ) override
    {
        m_session->lt_session().get_context().post( [=,this]() mutable {
        
            DBG_SINGLE_THREAD

            std::shared_ptr<sirius::drive::FlatDrive> pDrive;
            {
                if ( auto driveIt = m_driveMap.find(driveKey); driveIt != m_driveMap.end() )
                {
                    pDrive = driveIt->second;
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
        
            DBG_SINGLE_THREAD
            
            if ( const auto driveIt = m_driveMap.find(driveKey); driveIt != m_driveMap.end() )
            {
                driveIt->second->cancelModifyDrive( transactionHash );
                return;
            }

            _LOG( "cancelModify: unknown drive: " << driveKey );
        });//post
    }
    
//    std::string loadTorrent( const Key& driveKey, const InfoHash& infoHash ) override
//    {
//        DBG_SINGLE_THREAD
//
//        std::shared_ptr<sirius::drive::FlatDrive> pDrive;
//        {
//
//            const std::unique_lock<std::shared_mutex> lock(m_driveMutex);
//            if ( auto driveIt = m_driveMap.find(driveKey); driveIt != m_driveMap.end() )
//            {
//                pDrive = driveIt->second;
//            }
//            else {
//                return "drive not found";
//            }
//        }
//
//        pDrive->loadTorrent( infoHash );
//        return "";
//    }

    void asyncAddDownloadChannelInfo( Key driveKey, DownloadRequest&& request ) override
    {
        m_session->lt_session().get_context().post( [=,this]() mutable {
        
            DBG_SINGLE_THREAD

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
        DBG_SINGLE_THREAD
        
        removeChannelInfo(channelKey);
    }

    virtual void sendReceiptToOtherReplicators( const std::array<uint8_t,32>&  downloadChannelId,
                                                const std::array<uint8_t,32>&  clientPublicKey,
                                                uint64_t                       downloadedSize,
                                                const std::array<uint8_t,64>&  signature ) override
    {
        DBG_SINGLE_THREAD
        
        auto replicatorPublicKey = publicKey();

        // check receipt
        if ( !DownloadLimiter::verifyReceipt(  downloadChannelId,
                                               clientPublicKey,
                                               replicatorPublicKey,
                                               downloadedSize,
                                               signature ) )
        {
            //todo log error?
            std::cerr << "ERROR! Invalid receipt" << std::endl << std::flush;
            assert(0);
            return;
        }
        
        std::vector<uint8_t> message;
        message.insert( message.end(), downloadChannelId.begin(),   downloadChannelId.end() );
        message.insert( message.end(), clientPublicKey.begin(),     clientPublicKey.end() );
        message.insert( message.end(), replicatorPublicKey.begin(), replicatorPublicKey.end() );
        message.insert( message.end(), (uint8_t*)&downloadedSize,   ((uint8_t*)&downloadedSize)+8 );
        message.insert( message.end(), signature.begin(),           signature.end() );
        
        std::shared_lock<std::shared_mutex> lock(m_downloadChannelMutex);

        if ( auto it = m_downloadChannelMap.find(downloadChannelId); it != m_downloadChannelMap.end() )
        {
            // go throw replictor list
            for( auto replicatorIt = it->second.m_replicatorsList.begin(); replicatorIt != it->second.m_replicatorsList.end(); replicatorIt++ )
            {
                //_LOG( "todo++++ sendMessage(rcpt) " << m_dbgOurPeerName << " " << int(downloadChannelId[0]) );
                m_session->sendMessage( "rcpt", { replicatorIt->m_endpoint.address(), replicatorIt->m_endpoint.port() }, message );
            }
        }
    }

    virtual void asyncOnDownloadOpinionReceived( DownloadApprovalTransactionInfo anOpinion ) override
    {
        m_session->lt_session().get_context().post( [=,this]() mutable {

            DBG_SINGLE_THREAD

            if ( anOpinion.m_opinions.size() != 1 )
            {
                LOG_ERR( "onDownloadOpinionReceived: invalid opinion format: anOpinion.m_opinions.size() != 1" )
                return;
            }
        
            addOpinion( std::move(anOpinion) );
        });
    }

    void processDownloadOpinion( const DownloadApprovalTransactionInfo& anOpinion ) override
    {
        m_eventHandler.downloadOpinionHasBeenReceived(*this, anOpinion);
    }
    
    DownloadOpinion createMyOpinion( const DownloadChannelInfo& info )
    {
        DBG_SINGLE_THREAD

        DownloadOpinion myOpinion( publicKey() );

        for( const auto& replicatorIt : info.m_replicatorsList )
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
        DBG_SINGLE_THREAD

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
            LOG_ERR("Attempt to add opinion for a non-existing channel");
            return;
        }

        auto &channel = channelIt->second;
        auto blockHash = opinion.m_blockHash;

        if (channel.m_downloadOpinionMap.find(opinion.m_blockHash) == channel.m_downloadOpinionMap.end())
        {
            channel.m_downloadOpinionMap[blockHash] = DownloadOpinionMapValue
                    {
                            opinion.m_blockHash,
                            opinion.m_downloadChannelId,
                            {}
                    };
        }

        auto &opinionInfo = channel.m_downloadOpinionMap[blockHash];
        auto &opinions = opinionInfo.m_opinions;
        opinions[opinion.m_opinions[0].m_replicatorKey] = opinion.m_opinions[0];

        // check opinion number
        //_LOG( "///// " << opinionInfo.m_opinions.size() << " " <<  (opinionInfo.m_replicatorNumber*2)/3 );
        //todo not ">=..."!!! - "> (opinionInfo.m_replicatorNumber*2)/3
        if (opinions.size() >= (channel.m_replicatorsList.size() * 2) / 3)
        {
            // start timer if it is not started
            if (!opinionInfo.m_timer)
            {
                //todo check
                opinionInfo.m_timer = m_session->startTimer(m_downloadApprovalTransactionTimerDelayMs,
                                                            [this, &opinionInfo]()
                                                            { onDownloadApprovalTimeExipred(opinionInfo); });
            }
        }
    }
    
    void onDownloadApprovalTimeExipred( DownloadOpinionMapValue& mapValue )
    {
        DBG_SINGLE_THREAD
        
        if ( mapValue.m_approveTransactionSent || mapValue.m_approveTransactionReceived )
            return;

        // notify
        std::vector<DownloadOpinion> opinions;
        for (const auto& [replicatorId, opinion]: mapValue.m_opinions)
        {
            opinions.push_back(opinion);
        }
        auto transactionInfo = DownloadApprovalTransactionInfo{mapValue.m_eventHash, mapValue.m_downloadChannelId, std::move(opinions)};
        m_eventHandler.downloadApprovalTransactionIsReady( *this, transactionInfo );
        mapValue.m_approveTransactionSent = true;
    }
    
    virtual void asyncInitiateDownloadApprovalTransactionInfo( Hash256 blockHash, Hash256 channelId ) override
    {
        //todo make queue for several simultaneous requests of the same channelId

        m_session->lt_session().get_context().post( [=,this]() mutable {
        
            DBG_SINGLE_THREAD
            doInitiateDownloadApprovalTransactionInfo( blockHash, channelId );
        });//post
    }

    void doInitiateDownloadApprovalTransactionInfo( Hash256 blockHash, Hash256 channelId )
    {
        DBG_SINGLE_THREAD
        
        //todo make queue for several simultaneous requests of the same channelId
        
        if ( auto it = m_downloadChannelMap.find( channelId.array() ); it != m_downloadChannelMap.end() )
        {
            const auto& replicatorsList = it->second.m_replicatorsList;

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
            LOG_ERR( "channelId not found" );
        }
    }
    
    // It is called when drive is closing
    virtual void closeDriveChannels( const Hash256& blockHash, FlatDrive& drive ) override
    {
        DBG_SINGLE_THREAD

        bool deleteDriveImmediately = true;
        
        std::erase_if( m_downloadChannelMap, [](const auto& channelInfo )
        {
            return channelInfo.second.m_isModifyTx;
        });

        for( auto& [channelId,channelInfo] : m_downloadChannelMap )
        {
            if ( channelInfo.m_driveKey == drive.drivePublicKey().array() && !channelInfo.m_isModifyTx )
            {
                doInitiateDownloadApprovalTransactionInfo( blockHash, channelId );
                
                // drive will be deleted in 'asyncDownloadApprovalTransactionHasBeenPublished()'
                deleteDriveImmediately = false;
            }
        }
        
        
        if ( deleteDriveImmediately )
        {
            deleteDrive( drive.drivePublicKey().array() );
        }
    }

    void asyncDownloadApprovalTransactionHasFailedInvalidSignatures( Hash256 eventHash, Hash256 channelId ) override
    {
        m_session->lt_session().get_context().post( [=,this]() mutable {

            DBG_SINGLE_THREAD

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
                    opinionInfo.m_approveTransactionSent=false;
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
                    LOG_ERR( "eventHash not found" );
                }
            }
            else {
                LOG_ERR( "channelId not found" );
            }
        });//post
    }
    
    virtual void asyncDownloadApprovalTransactionHasBeenPublished( Hash256 eventHash, Hash256 channelId, bool driveIsClosed ) override
    {
        m_session->lt_session().get_context().post( [=,this]() mutable {
        
            DBG_SINGLE_THREAD
            
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
                    LOG_ERR( "eventHash not found" );
                }
            }
            else
            {
                LOG_ERR( "channelId not found" );
            }

            if ( !driveIsClosed )
            {
                return;
            }

            // Is it happened while drive is closing?
            if ( auto channelIt = m_downloadChannelMap.find( channelId.array() ); channelIt != m_downloadChannelMap.end() )
            {
                const auto& driveKey = channelIt->second.m_driveKey;

                if ( auto driveIt = m_driveMap.find( driveKey ); driveIt != m_driveMap.end() )
                {
                    bool driveWillBeDeleted = false;

                    if ( driveIt->second->closingTxHash() == eventHash )
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
        });//post
    }

    void deleteDrive( const std::array<uint8_t,32>& driveKey )
    {
        DBG_SINGLE_THREAD

        std::erase_if( m_downloadChannelMap, [&driveKey] (const auto& item) {
            return item.second.m_driveKey == driveKey;
        });

        std::erase_if( m_modifyDriveMap, [&driveKey] (const auto& item) {
            return item.second.m_driveKey == driveKey;
        });

        auto driveIt = m_driveMap.find( driveKey );
        assert( driveIt != m_driveMap.end() );

        driveIt->second->removeAllDriveData();
        
        m_driveMap.erase( driveIt );
    }
    
    virtual void asyncOnOpinionReceived( ApprovalTransactionInfo anOpinion ) override
    {
        m_session->lt_session().get_context().post( [=,this]() mutable {
            DBG_SINGLE_THREAD
        
            if ( auto it = m_driveMap.find( anOpinion.m_driveKey ); it != m_driveMap.end() )
            {
                it->second->onOpinionReceived( anOpinion );
            }
            else
            {
                LOG_ERR( "drive not found" );
            }
        });
    }


    void processOpinion( const ApprovalTransactionInfo& anOpinion ) override
    {
        m_eventHandler.opinionHasBeenReceived(*this, anOpinion);
    }
    
    virtual void asyncApprovalTransactionHasBeenPublished( ApprovalTransactionInfo transaction ) override
    {
        m_session->lt_session().get_context().post( [=,this]() mutable {
        
            DBG_SINGLE_THREAD

            if ( auto it = m_driveMap.find( transaction.m_driveKey ); it != m_driveMap.end() )
            {
                it->second->onApprovalTransactionHasBeenPublished( transaction );
            }
            else
            {
                LOG_ERR( "drive not found" );
            }
        });//post
    }

    void asyncApprovalTransactionHasFailedInvalidSignatures(Key driveKey, Hash256 transactionHash) override
    {
        m_session->lt_session().get_context().post( [=,this]() mutable {

            DBG_SINGLE_THREAD

            if ( auto it = m_driveMap.find( driveKey ); it != m_driveMap.end() )
            {
                it->second->onApprovalTransactionHasFailedInvalidSignatures( transactionHash );
            }
            else
            {
                LOG_ERR( "drive not found" );
            }
        });//post
    }
    
    virtual void asyncSingleApprovalTransactionHasBeenPublished( ApprovalTransactionInfo transaction ) override
    {
        m_session->lt_session().get_context().post( [=,this]() mutable {
        
            DBG_SINGLE_THREAD
            
            if ( auto it = m_driveMap.find( transaction.m_driveKey ); it != m_driveMap.end() )
            {
                it->second->onSingleApprovalTransactionHasBeenPublished( transaction );
            }
            else
            {
                LOG_ERR( "drive not found" );
            }
        });//post
    }
    
    virtual void sendMessage( const std::string& query, boost::asio::ip::tcp::endpoint endpoint, const std::string& message ) override
    {
        //todo? DBG_SINGLE_THREAD
        m_session->sendMessage( query, { endpoint.address(), endpoint.port() }, message );
    }
    
    virtual void onMessageReceived( const std::string& query, const std::string& message ) override try
    {
        DBG_SINGLE_THREAD
        
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
        LOG_ERR( "onMessageReceived: invalid message format: query=" << query );
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
        if ( auto it = m_driveMap.find(driveKey); it != m_driveMap.end() )
        {
            return it->second;
        }
        assert(0);
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

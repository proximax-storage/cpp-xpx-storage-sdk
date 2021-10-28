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

namespace sirius::drive {

//
// DefaultReplicator
//
class DefaultReplicator : public DownloadLimiter // Replicator
{
//todo++
public:
    std::shared_ptr<Session> m_session;

    // Drives
    std::map<Key, std::shared_ptr<sirius::drive::FlatDrive>> m_drives;
    std::shared_mutex  m_driveMutex;

    // Replicator's keys
    crypto::KeyPair m_keyPair;

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

    const char* m_dbgReplicatorName;
    
public:
    DefaultReplicator (
               crypto::KeyPair&& keyPair,
               std::string&& address,
               std::string&& port,
               std::string&& storageDirectory,
               std::string&& sandboxDirectory,
               bool          useTcpSocket,
               ReplicatorEventHandler& handler,
               const char*   dbgReplicatorName ) : DownloadLimiter( m_keyPair, dbgReplicatorName ),

        m_keyPair( std::move(keyPair) ),
        m_address( std::move(address) ),
        m_port( std::move(port) ),
        m_storageDirectory( std::move(storageDirectory) ),
        m_sandboxDirectory( std::move(sandboxDirectory) ),
        m_useTcpSocket( useTcpSocket ),
        m_eventHandler(handler),
        m_dbgReplicatorName( dbgReplicatorName )
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
        m_session->lt_session().m_dbgOurPeerName = m_dbgReplicatorName;
    }

    Hash256 getRootHash( const Key& driveKey ) override
    {
        std::shared_lock<std::shared_mutex> lock(m_driveMutex);
        if ( const auto driveIt = m_drives.find(driveKey); driveIt != m_drives.end() )
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
        if ( const auto driveIt = m_drives.find(driveKey); driveIt != m_drives.end() )
        {
            return driveIt->second->printDriveStatus();
        }

        LOG_ERR( "unknown dive: " << driveKey );
        throw std::runtime_error( std::string("unknown dive: ") + toString(driveKey.array()) );

    }


    std::string addDrive(const Key& driveKey, uint64_t driveSize, const ReplicatorList& replicators ) override
    {
        LOG( "adding drive " << driveKey );

        std::unique_lock<std::shared_mutex> lock(m_driveMutex);

        if (m_drives.find(driveKey) != m_drives.end()) {
            return "drive already added";
        }

        m_drives[driveKey] = sirius::drive::createDefaultFlatDrive(
                session(),
                m_storageDirectory,
                m_sandboxDirectory,
                driveKey,
                driveSize,
                m_eventHandler,
                *this,
                replicators );

        return "";
    }

    std::string removeDrive(const Key& driveKey) override
    {
        LOG( "removing drive " << driveKey );

        std::unique_lock<std::shared_mutex> lock(m_driveMutex);

        if (m_drives.find(driveKey) == m_drives.end())
            return "drive not found";

        m_drives.erase(driveKey);
        return "";
    }

    std::shared_ptr<sirius::drive::FlatDrive> getDrive( const Key& driveKey ) override {
        LOG( "getDrive " << driveKey );

        std::shared_lock<std::shared_mutex> lock(m_driveMutex);

        if (m_drives.find(driveKey) == m_drives.end())
        {
            LOG( "drive not found " << driveKey );
            return nullptr;
        }

        return m_drives[driveKey];
    }

    std::string modify( const Key& driveKey, ModifyRequest&& modifyRequest ) override
    {
        LOG( "drive modification:\ndrive: " << driveKey << "\n info hash: " << infoHash );

        const std::shared_lock<std::shared_mutex> lock(m_driveMutex);

        std::shared_ptr<sirius::drive::FlatDrive> pDrive;
        {
            if ( auto driveIt = m_drives.find(driveKey); driveIt != m_drives.end() )
            {
                pDrive = driveIt->second;
            }
            else {
                return "drive not found";
            }
        }
        
        addModifyDriveInfo( modifyRequest.m_transactionHash.array(),
                            driveKey,
                            modifyRequest.m_maxDataSize,
                            modifyRequest.m_clientPublicKey,
                            modifyRequest.m_replicatorList );


        ReplicatorList replicatorList2;
        for( auto it = modifyRequest.m_replicatorList.begin();  it != modifyRequest.m_replicatorList.end(); it++ )
        {
            if ( it->m_publicKey == publicKey() )
            {
                modifyRequest.m_replicatorList.erase( it );
                break;
            }
        }
        pDrive->startModifyDrive( std::move(modifyRequest) );
        return "";
    }
    
    std::string cancelModify( const Key&        driveKey,
                              const Hash256&    transactionHash ) override
    {
        std::shared_lock<std::shared_mutex> lock(m_driveMutex);
        
        if ( const auto driveIt = m_drives.find(driveKey); driveIt != m_drives.end() )
        {
            driveIt->second->cancelModifyDrive( transactionHash );
            return "";
        }

        LOG_ERR( "unknown drive: " << driveKey );
        throw std::runtime_error( std::string("unknown dive: ") + toString(driveKey.array()) );

        return "unknown drive";
    }
    
    std::string loadTorrent( const Key& driveKey, const InfoHash& infoHash ) override
    {
        LOG( "loadTorrent:\ndrive: " << driveKey << "\n info hash: " << infoHash );

        std::shared_ptr<sirius::drive::FlatDrive> pDrive;
        {
            const std::unique_lock<std::shared_mutex> lock(m_driveMutex);
            if ( auto driveIt = m_drives.find(driveKey); driveIt != m_drives.end() )
            {
                pDrive = driveIt->second;
            }
            else {
                return "drive not found";
            }
        }

        pDrive->loadTorrent( infoHash );
        return "";
    }

    void addDownloadChannelInfo( const std::array<uint8_t,32>&   channelKey,
                                 size_t                          prepaidDownloadSize,
                                 const Key&                      driveKey,
                                 const ReplicatorList&           replicatorsList,
                                 const std::vector<Key>&         clients ) override
    {
        std::vector<std::array<uint8_t,32>> clientList;
        for( const auto& it : clients )
            clientList.push_back( it.array() );
        addChannelInfo( channelKey, prepaidDownloadSize, driveKey, replicatorsList, clientList );
    }

    void removeDownloadChannelInfo( const std::array<uint8_t,32>& channelKey ) override
    {
        removeChannelInfo(channelKey);
    }

    virtual void sendReceiptToOtherReplicators( const std::array<uint8_t,32>&  downloadChannelId,
                                                const std::array<uint8_t,32>&  clientPublicKey,
                                                uint64_t                       downloadedSize,
                                                const std::array<uint8_t,64>&  signature ) override
    {
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
                m_session->sendMessage( "rcpt", { replicatorIt->m_endpoint.address(), replicatorIt->m_endpoint.port() }, message );
            }
        }
    }
    
    virtual void onDownloadOpinionReceived( DownloadApprovalTransactionInfo&& anOpinion ) override
    {
        if ( anOpinion.m_opinions.size() != 1 )
        {
            LOG_ERR( "onDownloadOpinionReceived: invalid opinion format: anOpinion.m_opinions.size() != 1" )
            return;
        }
        
        addOpinion( std::move(anOpinion) );
    }
    
    DownloadOpinion createMyOpinion( const DownloadChannelInfo& info )
    {
        DownloadOpinion myOpinion( publicKey() );

        for( const auto& replicatorIt : info.m_replicatorsList )
        {
            if ( auto downloadedIt = info.m_replicatorUploadMap.find( replicatorIt.m_publicKey.array()); downloadedIt != info.m_replicatorUploadMap.end() )
            {
                myOpinion.m_downloadedBytes.push_back( downloadedIt->second.m_uploadedSize );
            }
            else if ( replicatorIt.m_publicKey == publicKey() )
            {
                myOpinion.m_downloadedBytes.push_back( info.m_uploadedSize );
            }
            else
            {
                myOpinion.m_downloadedBytes.push_back( 0 );
            }
        }
        
        return myOpinion;
    }
    
    void addOpinion( DownloadApprovalTransactionInfo&& opinion )
    {
        std::unique_lock<std::shared_mutex> lock(m_downloadOpinionMutex);

        //
        // remove outdated entries (by m_creationTime)
        //
        auto now = boost::posix_time::microsec_clock::universal_time();
        
        for( auto it = m_downloadOpinionMap.begin(), last = m_downloadOpinionMap.end(); it != last; )
        {
            if ( (now - it->second.m_creationTime).seconds() > 60*60 )
            {
                it = m_downloadOpinionMap.erase(it);
            }
            else
            {
                it++;
            }
        }
        
        //
        // add opinion
        //
        if ( auto it = m_downloadOpinionMap.find( opinion.m_blockHash ); it != m_downloadOpinionMap.end() )
        {
            auto& opinionInfo = it->second.m_info;
            opinionInfo.m_opinions.push_back( opinion.m_opinions[0] );

            // check opinion number
            //_LOG( "///// " << opinionInfo.m_opinions.size() << " " <<  (opinionInfo.m_replicatorNumber*2)/3 );
            //todo not ">=..."!!! - "> (opinionInfo.m_replicatorNumber*2)/3
            if ( opinionInfo.m_opinions.size() >= (opinionInfo.m_replicatorNumber*2)/3 )
            {
                // start timer if it is not started
                if ( !it->second.m_timer )
                {
                    auto& opinionData = it->second;
                    //todo 10 miliseconds!!!
                    it->second.m_timer = m_session->startTimer( m_downloadApprovalTransactionTimerDelayMs,
                                            [this,&opinionData]() { onDownloadApprovalTimeExipred( opinionData ); } );
                }
            }
        }
        else
        {
            m_downloadOpinionMap.insert( { opinion.m_blockHash, DownloadOpinionMapValue{std::move(opinion)}} );
        }
    }
    
    void onDownloadApprovalTimeExipred( DownloadOpinionMapValue& mapValue )
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);

        if ( mapValue.m_approveTransactionSent || mapValue.m_approveTransactionReceived )
            return;

        // notify
        m_eventHandler.downloadApprovalTransactionIsReady( *this, mapValue.m_info );
        mapValue.m_approveTransactionSent = true;
    }
    
    virtual void prepareDownloadApprovalTransactionInfo( const Hash256& blockHash, const Hash256& channelId ) override
    {
        _LOG( "prepareDownloadApprovalTransactionInfo: " << dbgReplicatorName() );
        std::shared_lock<std::shared_mutex> lock(m_downloadChannelMutex);

        //todo make queue for several simultaneous requests of the same channelId
        
        if ( auto it = m_downloadChannelMap.find( channelId.array() ); it != m_downloadChannelMap.end() )
        {
            const auto& replicatorsList = it->second.m_replicatorsList;

            //
            // Create my opinion
            //
            
            auto myOpinion = createMyOpinion(it->second);
            lock.unlock();
            
            myOpinion.Sign( keyPair(), blockHash.array(), channelId.array() );
            
            DownloadApprovalTransactionInfo transactionInfo{  blockHash.array(),
                                                            channelId.array(),
                                                            (uint32_t)replicatorsList.size(),
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
        //_LOG( "//exit prepareDownloadApprovalTransactionInfo: " << dbgReplicatorName() );
    }
    
    virtual void onDownloadApprovalTransactionHasBeenPublished( const Hash256& blockHash, const Hash256& channelId ) override
    {
        std::shared_lock<std::shared_mutex> lock(m_downloadOpinionMutex);

        // clear opinion map
        if ( auto it = m_downloadOpinionMap.find( blockHash.array() ); it != m_downloadOpinionMap.end() )
        {
            if ( it->second.m_timer )
                it->second.m_timer.reset();
        }
        else
        {
            LOG_ERR( "channelId not found" );
        }
    }
    
    virtual void onOpinionReceived( const ApprovalTransactionInfo& anOpinion ) override
    {
        if ( auto it = m_drives.find( anOpinion.m_driveKey ); it != m_drives.end() )
        {
            it->second->onOpinionReceived( anOpinion );
        }
        else
        {
            LOG_ERR( "drive not found" );
        }
    }
    
    virtual void onApprovalTransactionHasBeenPublished( const ApprovalTransactionInfo& transaction ) override
    {
        if ( auto it = m_drives.find( transaction.m_driveKey ); it != m_drives.end() )
        {
            it->second->onApprovalTransactionHasBeenPublished( transaction );
        }
        else
        {
            LOG_ERR( "drive not found" );
        }
    }
    
    virtual void onSingleApprovalTransactionHasBeenPublished( const ApprovalTransactionInfo& transaction ) override
    {
        if ( auto it = m_drives.find( transaction.m_driveKey ); it != m_drives.end() )
        {
            it->second->onSingleApprovalTransactionHasBeenPublished( transaction );
        }
        else
        {
            LOG_ERR( "drive not found" );
        }

    }
    
    virtual void sendMessage( const std::string& query, boost::asio::ip::tcp::endpoint endpoint, const std::string& message ) override
    {
        m_session->sendMessage( query, { endpoint.address(), endpoint.port() }, message );
    }
    
    virtual void onMessageReceived( const std::string& query, const std::string& message ) override try
    {
        //todo
        if ( query == "opinion" )
        {
            std::istringstream is( message, std::ios::binary );
            cereal::PortableBinaryInputArchive iarchive(is);
            ApprovalTransactionInfo info;
            iarchive( info );
            
            if ( auto it = m_drives.find( info.m_driveKey ); it != m_drives.end() )
            {
                it->second->onOpinionReceived( info );
            }
            else
            {
                LOG_ERR( "onMessageReceived(opinion): drive not found" );
            }
            return;
        }
        else if ( query ==  "dnopinion" )
        {
            std::istringstream is( message, std::ios::binary );
            cereal::PortableBinaryInputArchive iarchive(is);
            DownloadApprovalTransactionInfo info;
            iarchive( info );

            onDownloadOpinionReceived( std::move(info) );
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
    
    const char* dbgReplicatorName() const override { return m_dbgReplicatorName; }
    
private:
    std::shared_ptr<sirius::drive::Session> session() {
        return m_session;
    }
};

std::shared_ptr<Replicator> createDefaultReplicator(
                                        crypto::KeyPair&&   keyPair,
                                        std::string&&       address,
                                        std::string&&       port,
                                        std::string&&       storageDirectory,
                                        std::string&&       sandboxDirectory,
                                        bool                useTcpSocket,
                                        ReplicatorEventHandler& handler,
                                        const char*         dbgReplicatorName )
{
    return std::make_shared<DefaultReplicator>(
                                               std::move(keyPair),
                                               std::move(address),
                                               std::move(port),
                                               std::move(storageDirectory),
                                               std::move(sandboxDirectory),
                                               useTcpSocket,
                                               handler,
                                               dbgReplicatorName );
}

}

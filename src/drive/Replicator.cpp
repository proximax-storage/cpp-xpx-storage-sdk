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

#include <libtorrent/alert_types.hpp>
#include <filesystem>

#include <mutex>

namespace sirius { namespace drive {

//
// DefaultReplicator
//
class DefaultReplicator : public DownloadLimiter // Replicator
{
    std::shared_ptr<Session> m_session;

    // Drives
    std::map<Key, std::shared_ptr<sirius::drive::FlatDrive>> m_drives;
    std::mutex  m_mutex;

    // Replicator's keys
    crypto::KeyPair m_keyPair;

    // Session listen interface
    std::string m_address;
    std::string m_port;

    // Folders for drives and sandboxes
    std::string m_storageDirectory;
    std::string m_sandboxDirectory;

    bool        m_useTcpSocket;

    const char* m_dbgReplicatorName;

public:
    DefaultReplicator (
               crypto::KeyPair&& keyPair,
               std::string&& address,
               std::string&& port,
               std::string&& storageDirectory,
               std::string&& sandboxDirectory,
               bool          useTcpSocket,
               const char*   dbgReplicatorName ) : DownloadLimiter( m_keyPair, dbgReplicatorName ),

        m_keyPair( std::move(keyPair) ),
        m_address( std::move(address) ),
        m_port( std::move(port) ),
        m_storageDirectory( std::move(storageDirectory) ),
        m_sandboxDirectory( std::move(sandboxDirectory) ),
        m_useTcpSocket( useTcpSocket ),
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
        std::unique_lock<std::mutex> lock(m_mutex);
        if ( const auto driveIt = m_drives.find(driveKey); driveIt != m_drives.end() )
        {
            auto rootHash = driveIt->second->rootDriveHash();
            LOG( "getRootHash of: " << driveKey << " -> " << rootHash );
            return rootHash;
        }

        LOG_ERR( "unknown drive: " << driveKey );
        throw std::runtime_error( std::string("unknown dive: ") + toString(driveKey.array()) );

        return Hash256();
    }

    void printDriveStatus( const Key& driveKey ) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if ( const auto driveIt = m_drives.find(driveKey); driveIt != m_drives.end() )
        {
            return driveIt->second->printDriveStatus();
        }

        LOG_ERR( "unknown dive: " << driveKey );
        throw std::runtime_error( std::string("unknown dive: ") + toString(driveKey.array()) );

    }


    std::string addDrive(const Key& driveKey, size_t driveSize) override
    {
        LOG( "adding drive " << driveKey );

        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_drives.find(driveKey) != m_drives.end()) {
            return "drive already added";
        }

        m_drives[driveKey] = sirius::drive::createDefaultFlatDrive(
                session(),
                m_storageDirectory,
                m_sandboxDirectory,
                arrayToString(driveKey),
                driveSize);

        return "";
    }

    std::string removeDrive(const Key& driveKey)
    {
        LOG( "removing drive " << driveKey );

        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_drives.find(driveKey) == m_drives.end())
            return "drive not found";

        m_drives.erase(driveKey);
        return "";
    }


    std::string modify( const Key&       driveKey,
                        const InfoHash&  infoHash,
                        const Hash256&   transactionHash,
                        uint64_t 		 maxDataSize,
                        const DriveModifyHandler& handler ) override
    {
        LOG( "drive modification:\ndrive: " << driveKey << "\n info hash: " << infoHash );

        std::shared_ptr<sirius::drive::FlatDrive> pDrive;
        {
            const std::unique_lock<std::mutex> lock(m_mutex);
            if ( auto driveIt = m_drives.find(driveKey); driveIt != m_drives.end() )
            {
                pDrive = driveIt->second;
            }
            else {
                return "drive not found";
            }
        }

        pDrive->startModifyDrive( infoHash, transactionHash, maxDataSize, handler );
        return "";
    }

    std::string loadTorrent( const Key& driveKey, const InfoHash& infoHash )
    {
        LOG( "loadTorrent:\ndrive: " << driveKey << "\n info hash: " << infoHash );

        std::shared_ptr<sirius::drive::FlatDrive> pDrive;
        {
            const std::unique_lock<std::mutex> lock(m_mutex);
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
                                 const ReplicatorList&           replicatorsList,
                                 std::vector<Key>&&              clients ) override
    {
        addChannelInfo( channelKey, prepaidDownloadSize, replicatorsList, std::move(clients) );
    }

    virtual void sendReceiptToOtherReplicators( const std::array<uint8_t,32>&  downloadChannelId,
                                                const std::array<uint8_t,32>&  clientPublicKey,
                                                uint64_t                       downloadedSize,
                                                const std::array<uint8_t,64>&  signature ) override
    {
        // verify receipt
        if ( !DownloadLimiter::verifyReceipt(  downloadChannelId,
                                               clientPublicKey,
                                               publicKey(),
                                               downloadedSize,
                                               signature ) )
        {
            //todo log error?
            std::cerr << "ERROR! Invalid receipt" << std::endl << std::flush;
            //assert(0);

            return;
        }
        
        // todo
        std::vector<uint8_t> message;
        message.insert( message.end(), downloadChannelId.begin(), downloadChannelId.end() );
        message.insert( message.end(), clientPublicKey.begin(),   clientPublicKey.end() );
        message.insert( message.end(), (uint8_t*)&downloadedSize, ((uint8_t*)&downloadedSize)+8 );
        message.insert( message.end(), signature.begin(),         signature.end() );

        //todo mutex
        if ( auto it = m_channelMap.find(downloadChannelId); it != m_channelMap.end() )
        {
            for( auto replicatorIt = it->second.m_replicatorsList.begin(); replicatorIt != it->second.m_replicatorsList.end(); replicatorIt++ )
            {
                m_session->sendMessage( "rcpt", { replicatorIt->m_endpoint.address(), replicatorIt->m_endpoint.port() }, message );
            }
        }
    }


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
                                        const char*         dbgReplicatorName )
{
    return std::make_shared<DefaultReplicator>(
                                               std::move(keyPair),
                                               std::move(address),
                                               std::move(port),
                                               std::move(storageDirectory),
                                               std::move(sandboxDirectory),
                                               useTcpSocket,
                                               dbgReplicatorName );
}

}}

/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include "drive/Replicator.h"
#include <sirius_drive/session_delegate.h>
#include "crypto/Signer.h"
#include "types.h"

#include <map>
#include <shared_mutex>

#define DBG_MAIN_THREAD { assert( m_dbgThreadId == std::this_thread::get_id() ); }

namespace sirius::drive {

//
// DownloadLimiter - it manages all user files at replicator side
//
class DownloadLimiter : public Replicator,
                        public lt::session_delegate,
                        public std::enable_shared_from_this<DownloadLimiter>
{
protected:
    std::shared_ptr<Session> m_session;

    // Replicator's keys
    const crypto::KeyPair& m_keyPair;

    std::shared_mutex   m_mutex;

    // Drives
    std::map<Key, std::shared_ptr<FlatDrive>> m_driveMap;
    std::shared_mutex  m_driveMutex;


    ChannelMap          m_downloadChannelMap;
    std::shared_mutex   m_downloadChannelMutex;

    ModifyDriveMap      m_modifyDriveMap;

    uint64_t            m_receiptLimit = 32*1024; //1024*1024;

    std::string         m_dbgOurPeerName = "unset";
    


    std::thread::id          m_dbgThreadId;

public:
    DownloadLimiter( const crypto::KeyPair& keyPair, const char* dbgOurPeerName ) : m_keyPair(keyPair), m_dbgOurPeerName(dbgOurPeerName)
    {
    }
    
    void printReport( const std::array<uint8_t,32>&  transactionHash )
    {
        std::shared_lock<std::shared_mutex> lock(m_downloadChannelMutex);
        
        if ( auto it = m_downloadChannelMap.find( transactionHash ); it != m_downloadChannelMap.end() )
        {
            _LOG( "requestedSize=" << it->second.m_requestedSize << "; uploadedSize=" << it->second.m_uploadedSize );
            return;
        }

        _LOG( dbgOurPeerName() << "ERROR: printReport hash: " << (int)transactionHash[0] );
        assert(0);
    }

    void onDisconnected( const std::array<uint8_t,32>&  transactionHash,
                         const std::array<uint8_t,32>&  peerPublicKey,
                         int                            siriusFlags ) override
    {
        DBG_MAIN_THREAD
        
        //TODO++
        return;
        
        std::shared_lock<std::shared_mutex> lock(m_mutex);

        if ( !(siriusFlags & lt::sf_is_receiver) )
        {
            _LOG( "onDisconnected: " << dbgOurPeerName() << " from client: " << (int)peerPublicKey[0] );
        }
        else
        {
            _LOG( "onDisconnected: " << dbgOurPeerName() << " from peer: " << (int)peerPublicKey[0] );
            for( const auto& i : m_modifyDriveMap ) {
                _LOG( "m_modifyDriveMap: " << (int)i.first[0] << " " << (int)transactionHash[0]);
            }

            if ( const auto& it = m_modifyDriveMap.find( transactionHash ); it != m_modifyDriveMap.end() )
            {
                for( const auto& replicatorIt : it->second.m_modifyTrafficMap )
                {
                    _LOG( " m_receivedSize: " <<  replicatorIt.second.m_receivedSize << " from " << (int)replicatorIt.first[0] );
                    _LOG( " m_sentSize: "     <<  replicatorIt.second.m_sentSize << " to " << (int)replicatorIt.first[0] );
                }
                _LOG( " ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ " );
            }
        }
    }

    void printTrafficDistribution( const std::array<uint8_t,32>&  transactionHash ) override
    {
      m_session->lt_session().get_context().post( [=,this]() mutable {

        if ( const auto& it = m_modifyDriveMap.find( transactionHash ); it != m_modifyDriveMap.end() )
        {
            _LOG( "\nTrafficDistribution: " << dbgOurPeerName() << " (" << (int)publicKey()[0] << ")" );
            for( const auto& replicatorIt : it->second.m_modifyTrafficMap )
            {
                if ( replicatorIt.second.m_receivedSize )
                {
                    _LOG( " receivedSize: " <<  replicatorIt.second.m_receivedSize << " from " << (int)replicatorIt.first[0] );
                }
                if ( replicatorIt.second.m_requestedSize )
                {
                    _LOG( " requestedSize: "     <<  replicatorIt.second.m_requestedSize << " by " << (int)replicatorIt.first[0] );
                }
                if ( replicatorIt.second.m_sentSize )
                {
                    _LOG( " sentSize: "     <<  replicatorIt.second.m_sentSize << " to " << (int)replicatorIt.first[0] );
                }
            }
        }
      });
    }
    
    //TODO return const ModifyDriveInfo getMyDownloadOpinion& ???
    //TODO make it async !!!
    virtual ModifyDriveInfo getMyDownloadOpinion( const Hash256& transactionHash ) const override
    {
        DBG_MAIN_THREAD

        if ( const auto it = m_modifyDriveMap.find( transactionHash.array() ); it != m_modifyDriveMap.end() )
        {
            return it->second;
        }
        LOG_ERR( "getMyDownloadOpinion: unknown modify transaction hash" );
        
        //(+++) ???
        return {};
    }


    bool checkDownloadLimit( const std::array<uint8_t,64>& /*signature*/,
                             const std::array<uint8_t,32>& downloadChannelId,
                            uint64_t                       downloadedSizeByClient ) override
    {
        DBG_MAIN_THREAD
        //std::shared_lock<std::shared_mutex> lock(m_downloadChannelMutex);
        
        if ( auto it = m_downloadChannelMap.find( downloadChannelId ); it != m_downloadChannelMap.end() )
        {
            if ( it->second.m_uploadedSize > downloadedSizeByClient + m_receiptLimit )
                return false;
            return true;
        }
        return false;
    }

    uint8_t getUploadedSize( const std::array<uint8_t,32>& downloadChannelId ) override
    {
        DBG_MAIN_THREAD
        //std::shared_lock<std::shared_mutex> lock(m_downloadChannelMutex);

        if ( auto it = m_downloadChannelMap.find( downloadChannelId ); it != m_downloadChannelMap.end() )
        {
            return it->second.m_uploadedSize;
        }
        return 0;
    }

    void addChannelInfo( const std::array<uint8_t,32>&  channelId,
                         uint64_t                       prepaidDownloadSize,
                         const Key&                     driveKey,
                         const ReplicatorList&          replicatorsList,
                         const std::vector<std::array<uint8_t,32>>&  clients )
    {
        DBG_MAIN_THREAD
        
        if ( auto it = m_downloadChannelMap.find(channelId); it != m_downloadChannelMap.end() )
        {
            if ( it->second.m_prepaidDownloadSize <= prepaidDownloadSize )
            {
                LOG_ERR( "addChannelInfo: invalid prepaidDownloadSize: " << it->second.m_prepaidDownloadSize << " <= " << prepaidDownloadSize );
            }
            it->second.m_prepaidDownloadSize = prepaidDownloadSize;

//            Possible problem with receipts
//            /// Change client list of existing clients
//            if ( !clients.empty() )
//            {
//                it->second.m_clients = clients;
//            }
            return;
        }

        // Libtorrent has no access to sirius::Key.
        // We prepare upload map that contains information about how much each Replicator has uploaded
        ReplicatorUploadMap map;
        for( const auto& it : replicatorsList )
        {
            if ( it.m_publicKey.array() != publicKey() )
                map.insert( { it.m_publicKey.array(), {}} );
        }
        
//        _LOG( m_dbgOurPeerName <<  " addChInfo: " << int(channelId[0]) );
        
        m_downloadChannelMap[channelId] = DownloadChannelInfo{ false, prepaidDownloadSize, 0, 0, driveKey.array(), replicatorsList, map, clients, {} };
    }

    void addModifyDriveInfo( const Key&             modifyTransactionHash,
                             const Key&             driveKey,
                             uint64_t               dataSize,
                             const Key&             clientPublicKey,
                             const ReplicatorList&  replicatorsList )
    {
        DBG_MAIN_THREAD
        //std::unique_lock<std::shared_mutex> lock(m_mutex);

        ModifyTrafficMap trafficMap;
        trafficMap.insert( { clientPublicKey.array(), {0,0}} );
        
        std::vector<std::array<uint8_t,32>> clients;
        for( const auto& it : replicatorsList )
        {
            if ( it.m_publicKey.array() != publicKey() )
            {
                //_LOG( dbgOurPeerName() << " pubKey: " << (int)it.m_publicKey.array()[0] );
                trafficMap.insert( { it.m_publicKey.array(), {0,0}} );
                clients.emplace_back( it.m_publicKey.array() );
            }
        }
        
        m_modifyDriveMap[modifyTransactionHash.array()] = ModifyDriveInfo{ driveKey.array(), dataSize, replicatorsList, trafficMap, 0 };
        //lock.unlock();

        // we need to add modifyTransactionHash into 'm_downloadChannelMap'
        // because replicators could download pieces from their neighbors
        //
        {
            std::unique_lock<std::shared_mutex> lock(m_downloadChannelMutex);
            m_downloadChannelMap[modifyTransactionHash.array()] = DownloadChannelInfo{ true, dataSize, 0, 0, driveKey.array(), replicatorsList, {}, clients, {}};
        }
    }
    
    void removeModifyDriveInfo( const std::array<uint8_t,32>& modifyTransactionHash ) override
    {
        DBG_MAIN_THREAD
        //std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_modifyDriveMap.erase(modifyTransactionHash);
    }

    bool isPeerReplicator( const FlatDrive& drive, const std::array<uint8_t,32>&  peerPublicKey )
    {
        DBG_MAIN_THREAD
        
        auto& replicatorList = drive.replicatorList();
        auto replicatorIt = std::find_if( replicatorList.begin(), replicatorList.end(), [&peerPublicKey] (const auto& it) {
            return it.m_publicKey.array() == peerPublicKey;
        });
        
        //_LOG( m_dbgOurPeerName << ": isPeerReplicator(): peerPublicKey: " << Key(peerPublicKey) );

        return replicatorIt != replicatorList.end();
    }
    
    bool acceptConnection( const std::array<uint8_t,32>&  transactionHash,
                           const std::array<uint8_t,32>&  peerPublicKey,
                           bool*                          outIsDownloadUnlimited ) override
    {
        DBG_MAIN_THREAD
        
        if ( auto it = m_downloadChannelMap.find( transactionHash ); it != m_downloadChannelMap.end() )
        {
            if ( it->second.m_isModifyTx )
            {
                if ( auto driveIt = m_driveMap.find( it->second.m_driveKey ); driveIt != m_driveMap.end() )
                {
                    if ( isPeerReplicator( *driveIt->second, peerPublicKey) )
                    {
                        *outIsDownloadUnlimited = true;
                        return true;
                    }
                    else
                    {
                        //LOG_ERR( "acceptConnection: unknown peerPublicKey: " << sirius::Key(peerPublicKey) );
                        return false;
                    }
                }
                else
                {
                    LOG_ERR( "acceptConnection: unknown drive: " << sirius::Key(it->second.m_driveKey) );
                    return false;
                }
            }
            else // it is connection for download channel
            {
                auto& clients = it->second.m_clients;
                auto clientIt = std::find( clients.begin(), clients.end(), peerPublicKey);
                return clientIt != clients.end();
            }
            
            return false;
        }

        //TODO!!!
        return true;
//        if ( auto driveIt = m_driveMap.find( transactionHash ); driveIt != m_driveMap.end() )
//        {
//            if ( isPeerReplicator( *driveIt->second, peerPublicKey) )
//            {
//                *outIsDownloadUnlimited = true;
//                return true;
//            }
//        }
        
        _LOG( "bad connection? to: " << dbgOurPeerName() << " from: " << int(peerPublicKey[0]) << " hash: " << (int)transactionHash[0] );
//        assert(0);
        return false;
    }

    void onPieceRequest( const std::array<uint8_t,32>&  transactionHash,
                         const std::array<uint8_t,32>&  receiverPublicKey,
                         uint64_t                       pieceSize ) override
    {
        //std::unique_lock<std::shared_mutex> lock(m_downloadChannelMutex);
        DBG_MAIN_THREAD

        if ( auto it = m_downloadChannelMap.find( transactionHash ); it != m_downloadChannelMap.end() )
        {
            it->second.m_requestedSize += pieceSize;
            return;
        }

        if ( auto driveIt = m_driveMap.find( transactionHash ); driveIt != m_driveMap.end() )
        {
            if ( isPeerReplicator( *driveIt->second, receiverPublicKey) )
            {
                //todo it is a late replicator
                return;
            }
        }

        LOG_ERR( "ERROR: unknown transactionHash: " << (int)transactionHash[0] );
    }
    
    bool onPieceRequestReceived( const std::array<uint8_t,32>&  transactionHash,
                                 const std::array<uint8_t,32>&  receiverPublicKey,
                                 uint64_t                       pieceSize ) override
    {
        DBG_MAIN_THREAD
        //std::shared_lock<std::shared_mutex> lock(m_downloadChannelMutex);

        if ( auto it = m_downloadChannelMap.find( transactionHash ); it != m_downloadChannelMap.end() )
        {
            it->second.m_requestedSize += pieceSize;
            return true;
        }

        if ( auto it = m_modifyDriveMap.find( transactionHash ); it != m_modifyDriveMap.end() )
        {
            if ( auto peerIt = it->second.m_modifyTrafficMap.find(receiverPublicKey);  peerIt != it->second.m_modifyTrafficMap.end() )
            {
                peerIt->second.m_requestedSize += pieceSize;
                return true;
            }
            LOG_ERR( "ERROR: unknown peer: " << (int)receiverPublicKey[0] );
            return false;
        }
        
        if ( auto driveIt = m_driveMap.find( transactionHash ); driveIt != m_driveMap.end() )
        {
            if ( isPeerReplicator( *driveIt->second, receiverPublicKey) )
            {
                //todo it is a late replicator
                return true;
            }
        }

        _LOG( "!!!ERROR: unknown transactionHash(onPieceRequestReceived): " << (int)transactionHash[0] << " from: " );
        return false;
    }

    void onPieceSent( const std::array<uint8_t,32>&  transactionHash,
                      const std::array<uint8_t,32>&  receiverPublicKey,
                      uint64_t                       pieceSize ) override
    {
        DBG_MAIN_THREAD
        //std::shared_lock<std::shared_mutex> lock(m_downloadChannelMutex);

        // May be this piece was sent to client (during data download)
        if ( auto it = m_downloadChannelMap.find( transactionHash ); it != m_downloadChannelMap.end() )
        {
            it->second.m_uploadedSize += pieceSize;
            return;
        }

        // May be this piece was sent to another replicator (during drive modification)
        if ( auto it = m_modifyDriveMap.find( transactionHash ); it != m_modifyDriveMap.end() )
        {
            if ( auto peerIt = it->second.m_modifyTrafficMap.find(receiverPublicKey);  peerIt != it->second.m_modifyTrafficMap.end() )
            {
                peerIt->second.m_sentSize += pieceSize;
                return;
            }
            //LOG_ERR( "ERROR: unknown peer: " << (int)receiverPublicKey[0] );
        }

        //LOG_ERR( "ERROR(2): unknown transactionHash: " << (int)transactionHash[0] );
    }
    
    void onPieceReceived( const std::array<uint8_t,32>&  transactionHash,
                          const std::array<uint8_t,32>&  senderPublicKey,
                          uint64_t                       pieceSize ) override
    {
        DBG_MAIN_THREAD
        //std::shared_lock<std::shared_mutex> lock(m_mutex);

        if ( auto it = m_modifyDriveMap.find( transactionHash ); it != m_modifyDriveMap.end() )
        {
            if ( auto peerIt = it->second.m_modifyTrafficMap.find(senderPublicKey);  peerIt != it->second.m_modifyTrafficMap.end() )
            {
                peerIt->second.m_receivedSize  += pieceSize;
                it->second.m_totalReceivedSize += pieceSize;
                return;
            }
            
            LOG_ERR( "ERROR: unknown peer: " << (int)senderPublicKey[0] );
        }

        if ( auto driveIt = m_driveMap.find( transactionHash ); driveIt != m_driveMap.end() )
        {
            if ( isPeerReplicator( *driveIt->second, senderPublicKey ) )
            {
                //todo it is a late replicator
                return;
            }
        }
        
        _LOG( "unknown transactionHash: " << (int)transactionHash[0] );
        LOG_ERR( "ERROR(3): unknown transactionHash: " << (int)transactionHash[0] );
    }


    // will be called when one replicator informs another about downloaded size by client
    virtual void acceptReceiptFromAnotherReplicator( const std::array<uint8_t,32>&  downloadChannelId,
                                                     const std::array<uint8_t,32>&  clientPublicKey,
                                                     const std::array<uint8_t,32>&  replicatorPublicKey,
                                                     uint64_t                       downloadedSize,
                                                     const std::array<uint8_t,64>&  signature ) override
    {
        DBG_MAIN_THREAD
        
        // verify receipt
        if ( !verifyReceipt(  downloadChannelId,
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

        //todo accept
        std::unique_lock<std::shared_mutex> lock(m_downloadChannelMutex);
        if ( auto it = m_downloadChannelMap.find( downloadChannelId ); it != m_downloadChannelMap.end() )
        {
            // check client key
            if ( !it->second.m_isModifyTx )
            {
                const auto& v = it->second.m_clients;
                if ( std::find_if( v.begin(), v.end(), [&clientPublicKey](const auto& element)
                                  { return element == clientPublicKey; } ) == v.end() )
                {
                    LOG_ERR( "acceptReceiptFromAnotherReplicator: bad client key; it is ignored" );
                    return;
                }
            }

            auto replicatorIt = it->second.m_replicatorUploadMap.find( replicatorPublicKey );
            
            if ( replicatorIt != it->second.m_replicatorUploadMap.end() )
            {
                replicatorIt->second.m_uploadedSize = downloadedSize;
            }
            else
            {
                // check replicator key
                const auto& v = it->second.m_replicatorsList;
                if ( std::find_if( v.begin(), v.end(), [&replicatorPublicKey](const auto& element)
                                  { return element.m_publicKey.array() == replicatorPublicKey; } ) == v.end() )
                {
                    LOG_ERR( "acceptReceiptFromAnotherReplicator: bad replicator key; it is ignored" );
                    return;
                }
                it->second.m_replicatorUploadMap[replicatorPublicKey] = {downloadedSize};
            }
        }
        else
        {
            _LOG( dbgOurPeerName() << ": acceptReceiptFromAnotherReplicator: unknown channelId (maybe we are late): " << int(downloadChannelId[0]) << " " << int(replicatorPublicKey[0]) );
        }

        return;
    }
    
    void removeChannelInfo( const std::array<uint8_t,32>& channelId )
    {
        DBG_MAIN_THREAD
        //std::unique_lock<std::shared_mutex> lock(m_downloadChannelMutex);
        m_downloadChannelMap.erase( channelId );
    }

    bool isClient() const override { return false; }

    void signHandshake( const uint8_t* bytes, size_t size, std::array<uint8_t,64>& signature ) override
    {
        DBG_MAIN_THREAD
        crypto::Sign( m_keyPair, utils::RawBuffer{bytes,size}, reinterpret_cast<Signature&>(signature) );
        //_LOG( "SIGN HANDSHAKE: " << int(signature[0]) )
    }

    bool verifyHandshake( const uint8_t*                 bytes,
                          size_t                         size,
                          const std::array<uint8_t,32>&  publicKey,
                          const std::array<uint8_t,64>&  signature ) override
    {
        DBG_MAIN_THREAD
        
        //_LOG( "verifyHandshake: " << int(signature[0]) )
        bool ok = crypto::Verify( publicKey, utils::RawBuffer{bytes,size}, signature );;
        if ( !ok ) {
            LOG_ERR( "PROOBLEMS " );
        }
        return crypto::Verify( publicKey, utils::RawBuffer{bytes,size}, signature );
    }
    
    void signReceipt( const std::array<uint8_t,32>& downloadChannelId,
                      const std::array<uint8_t,32>& replicatorPublicKey,
                      uint64_t                      downloadedSize,
                      std::array<uint8_t,64>&       outSignature ) override
    {
        DBG_MAIN_THREAD
        // not used
        crypto::Sign( m_keyPair,
                      {
                        utils::RawBuffer{downloadChannelId},
                        utils::RawBuffer{m_keyPair.publicKey()},
                        utils::RawBuffer{replicatorPublicKey},
                        utils::RawBuffer{(const uint8_t*)&downloadedSize,8}
                      },
                      reinterpret_cast<Signature&>(outSignature) );
    }
    
    bool verifyReceipt(  const std::array<uint8_t,32>&  downloadChannelId,
                         const std::array<uint8_t,32>&  clientPublicKey,
                         const std::array<uint8_t,32>&  replicatorPublicKey,
                         uint64_t                       downloadedSize,
                         const std::array<uint8_t,64>&  signature ) override
    {
        DBG_MAIN_THREAD
        return crypto::Verify( clientPublicKey,
                               {
                                    utils::RawBuffer{downloadChannelId},
                                    utils::RawBuffer{clientPublicKey},
                                    utils::RawBuffer{replicatorPublicKey},
                                    utils::RawBuffer{(const uint8_t*)&downloadedSize,8}
                               },
                               reinterpret_cast<const Signature&>(signature) );
    }

    const std::array<uint8_t,32>& publicKey() override
    {
        return m_keyPair.publicKey().array();
    }

    const Key& replicatorKey() const override
    {
        return m_keyPair.publicKey();
    }

    virtual const crypto::KeyPair& keyPair() const override
    {
        return m_keyPair;
    }

//    void setStartReceivedSize( uint64_t /*downloadedSize*/ ) override
//    {
//    }

    uint64_t receivedSize( const std::array<uint8_t,32>&  peerPublicKey ) override
    {
        DBG_MAIN_THREAD
        return 0;
    }

    uint64_t requestedSize( const std::array<uint8_t,32>&  peerPublicKey ) override
    {
        DBG_MAIN_THREAD
        return 0;
    }

    const char* dbgOurPeerName() override
    {
        return m_dbgOurPeerName.c_str();
    }

    uint64_t receiptLimit() const override
    {
        return m_receiptLimit;
    }

    // may be, it will be used to increase or decrease limit
    void setReceiptLimit( uint64_t newLimitInBytes ) override
    {
        m_receiptLimit = newLimitInBytes;
    }
};

}

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
#include <iostream>
#include <fstream>
#include <sstream>

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

    ChannelMap          m_downloadChannelMap; // will be saved only if not crashed
    ChannelMap          m_downloadChannelMapBackup;
    ModifyDriveMap      m_modifyDriveMap;

    uint64_t            m_receiptLimit = 1024 * 1024;
    uint64_t            m_advancePaymentLimit = 50 * 1024 * 1024;

    std::string         m_dbgOurPeerName = "unset";
    
    // Drives
    std::map<Key, std::shared_ptr<FlatDrive>> m_driveMap;
    std::shared_mutex  m_driveMutex;

    std::thread::id     m_dbgThreadId;

public:
    DownloadLimiter( const crypto::KeyPair& keyPair, const char* dbgOurPeerName ) : m_keyPair(keyPair), m_dbgOurPeerName(dbgOurPeerName)
    {
    }
    
    void printReport( const std::array<uint8_t,32>&  transactionHash )
    {
        m_session->lt_session().get_context().post( [=,this]() mutable {
            
            if ( auto it = m_downloadChannelMap.find( transactionHash ); it != m_downloadChannelMap.end() )
            {
                _LOG( "requestedSize=" << it->second.m_requestedSize << "; uploadedSize=" << it->second.m_uploadedSize );
                return;
            }

            _LOG( dbgOurPeerName() << "ERROR: printReport hash: " << (int)transactionHash[0] );
            assert(0);
        });
    }

    void onDisconnected( const std::array<uint8_t,32>&  transactionHash,
                         const std::array<uint8_t,32>&  peerPublicKey,
                         int                            siriusFlags ) override
    {
        DBG_MAIN_THREAD
        
        //TODO++
        return;
        
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

            DBG_MAIN_THREAD
              
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
    
    virtual ModifyDriveInfo getMyDownloadOpinion( const Hash256& transactionHash ) const override
    {
        DBG_MAIN_THREAD

        if ( const auto it = m_modifyDriveMap.find( transactionHash.array() ); it != m_modifyDriveMap.end() )
        {
            return it->second;
        }
        _LOG_ERR( "getMyDownloadOpinion: unknown modify transaction hash" );
        
        return {};
    }


    bool checkDownloadLimit( const std::array<uint8_t,64>& /*signature*/,
                             const std::array<uint8_t,32>& downloadChannelId,
                             uint64_t /*downloadedSize*/) override
    {
        DBG_MAIN_THREAD

        auto it = m_downloadChannelMap.find( downloadChannelId );

        if ( it == m_downloadChannelMap.end() )
        {
            _LOG ("Check Download Limit:  No Such a Download Channel");
            return false;
        }

        auto replicatorIt = it->second.m_replicatorUploadMap.find( publicKey() );

        if( replicatorIt == it->second.m_replicatorUploadMap.end() )
        {
            _LOG ("Check Download Limit:  No Such a Replicator");
            return false;
        }

        if ( it->second.m_uploadedSize > replicatorIt->second.m_uploadedSize + m_receiptLimit )
        {
            _LOG ("Check Download Limit:  Receipts Size Exceeded");
            return false;
        }
        return true;
    }

    uint8_t getUploadedSize( const std::array<uint8_t,32>& downloadChannelId ) override
    {
        DBG_MAIN_THREAD

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
            // It is 'upgrade' of existing 'downloadChannel'

            if ( it->second.m_prepaidDownloadSize <= prepaidDownloadSize )
            {
                _LOG_ERR( "addChannelInfo: invalid prepaidDownloadSize: " << it->second.m_prepaidDownloadSize << " <= " << prepaidDownloadSize );
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
            if ( it.array() != publicKey() )
                map.insert( { it.array(), {}} );
        }
        
        m_downloadChannelMap[channelId] = DownloadChannelInfo{ false, prepaidDownloadSize, 0, 0, 0, driveKey.array(), replicatorsList, map, clients, {} };

        if ( auto backupIt = m_downloadChannelMapBackup.find(channelId); backupIt != m_downloadChannelMapBackup.end() )
        {
            // It will happen after restart only,
            // when 'storage-extension' inform us of the existing 'downloadChannels'

            // TODO remove this in future
            if ( backupIt->second.m_prepaidDownloadSize < prepaidDownloadSize )
            {
                _LOG_WARN( "addChannelInfo: channel size increased" );
            }

            if ( backupIt->second.m_prepaidDownloadSize > prepaidDownloadSize )
            {
                _LOG_ERR( "addChannelInfo: channel size decreased" );
            }

            auto it = m_downloadChannelMap.find(channelId);

            //(+++)??? restore backup info
            it->second.m_requestedSize        = backupIt->second.m_requestedSize;
            it->second.m_uploadedSize         = backupIt->second.m_uploadedSize;
            it->second.m_replicatorUploadMap  = backupIt->second.m_replicatorUploadMap;
            it->second.m_downloadOpinionMap   = backupIt->second.m_downloadOpinionMap;

            m_downloadChannelMapBackup.erase(backupIt);
        }
    }

    void addModifyDriveInfo( const Key&                 modifyTransactionHash,
                             const Key&                 driveKey,
                             uint64_t                   dataSize,
                             const Key&                 clientPublicKey,
                             const std::vector<Key>&    replicatorsList )
    {
        DBG_MAIN_THREAD

        auto driveMapIt = m_modifyDriveMap.lower_bound(modifyTransactionHash.array());

        if (driveMapIt != m_modifyDriveMap.end() && driveMapIt->first == modifyTransactionHash.array())
        {
            // already exists
            return;
        }

        ModifyTrafficMap trafficMap;
        trafficMap.insert( { clientPublicKey.array(), {0,0}} );
        
        std::vector<std::array<uint8_t,32>> clients;
        for( const auto& it : replicatorsList )
        {
            if ( it.array() != publicKey() )
            {
                //_LOG( dbgOurPeerName() << " pubKey: " << (int)it.m_publicKey.array()[0] );
                trafficMap.insert( { it.array(), {0,0}} );
                clients.emplace_back( it.array() );
            }
        }
        
        m_modifyDriveMap.insert(driveMapIt, {modifyTransactionHash.array(), ModifyDriveInfo{ driveKey.array(), dataSize, replicatorsList, trafficMap, 0 }});

        // we need to add modifyTransactionHash into 'm_downloadChannelMap'
        // because replicators could download pieces from their neighbors
        //
        {
            _LOG( "driveKey: " << driveKey )
            m_downloadChannelMap[modifyTransactionHash.array()] = DownloadChannelInfo{ true, dataSize, 0, 0, 0, driveKey.array(), replicatorsList, {}, clients, {}};
        }
    }
    
    void removeModifyDriveInfo( const std::array<uint8_t,32>& modifyTransactionHash ) override
    {
        DBG_MAIN_THREAD

        m_modifyDriveMap.erase(modifyTransactionHash);
    }

    bool isPeerReplicator( const FlatDrive& drive, const std::array<uint8_t,32>&  peerPublicKey )
    {
        DBG_MAIN_THREAD
        
        auto& replicatorList = drive.replicatorList();
        auto replicatorIt = std::find( replicatorList.begin(), replicatorList.end(), peerPublicKey);
        //_LOG( m_dbgOurPeerName << ": isPeerReplicator(): peerPublicKey: " << Key(peerPublicKey) );

        return replicatorIt != replicatorList.end();
    }

    bool isPeerClient( const FlatDrive& drive, const std::array<uint8_t,32>&  peerPublicKey )
    {
        return drive.getClient() == peerPublicKey;
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
                if ( auto drive = getDrive( it->second.m_driveKey ); drive )
                {
                    if ( isPeerReplicator( *drive, peerPublicKey) )
                    {
                        *outIsDownloadUnlimited = true;
                        return true;
                    }
                    else if ( isPeerClient( *drive, peerPublicKey) )
                    {
                        return true;
                    }
                    else
                    {
                        _LOG_WARN( "acceptConnection: unknown peerPublicKey: " << sirius::Key(peerPublicKey) );
                        return false;
                    }
                }
                else
                {
                    _LOG_WARN( "acceptConnection: unknown drive: " << sirius::Key(it->second.m_driveKey) );
                    return false;
                }
            }
            else // it is connection for download channel
            {
                auto& clients = it->second.m_clients;
                auto clientIt = std::find( clients.begin(), clients.end(), peerPublicKey);
                if ( clientIt == clients.end() ) {
                    return false;
                }
                return it->second.m_totalReceiptsSize < it->second.m_prepaidDownloadSize;
            }
        }

        return false;

//        //TODO!!!
//        return true;
        
//        if ( auto drive = getDrive( transactionHash ); drive )
//        {
//            if ( isPeerReplicator( *drive, peerPublicKey) )
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
        DBG_MAIN_THREAD

        if ( auto it = m_downloadChannelMap.find( transactionHash ); it != m_downloadChannelMap.end() )
        {
            it->second.m_requestedSize += pieceSize;
            return;
        }

        if ( auto drive = getDrive( transactionHash ); drive )
        {
            if ( isPeerReplicator( *drive, receiverPublicKey) )
            {
                //todo it is a late replicator
                return;
            }
        }

        _LOG_WARN( "ERROR: unknown transactionHash: " << (int)transactionHash[0] );
    }
    
    void onPieceRequestReceived( const std::array<uint8_t,32>&  transactionHash,
                                 const std::array<uint8_t,32>&  receiverPublicKey,
                                 uint64_t                       pieceSize ) override
    {
        DBG_MAIN_THREAD

        if ( auto it = m_downloadChannelMap.find( transactionHash ); it != m_downloadChannelMap.end() )
        {
            it->second.m_requestedSize += pieceSize;
        }

        if ( auto it = m_modifyDriveMap.find( transactionHash ); it != m_modifyDriveMap.end() )
        {
            if ( auto peerIt = it->second.m_modifyTrafficMap.find(receiverPublicKey);  peerIt != it->second.m_modifyTrafficMap.end() )
            {
                peerIt->second.m_requestedSize += pieceSize;
            }
        }
    }

    void onPieceSent( const std::array<uint8_t,32>&  transactionHash,
                      const std::array<uint8_t,32>&  receiverPublicKey,
                      uint64_t                       pieceSize ) override
    {
        DBG_MAIN_THREAD

        // Maybe this piece was sent to client (during data download)
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
            _LOG_WARN( "unknown peer: " << (int)receiverPublicKey[0] );
        }

        _LOG_WARN( "unknown transactionHash: " << (int)transactionHash[0] );
    }
    
    void onPieceReceived( const std::array<uint8_t,32>&  transactionHash,
                          const std::array<uint8_t,32>&  senderPublicKey,
                          uint64_t                       pieceSize ) override
    {
        DBG_MAIN_THREAD

        if ( auto it = m_modifyDriveMap.find( transactionHash ); it != m_modifyDriveMap.end() )
        {
            if ( auto peerIt = it->second.m_modifyTrafficMap.find(senderPublicKey);  peerIt != it->second.m_modifyTrafficMap.end() )
            {
                peerIt->second.m_receivedSize  += pieceSize;
                it->second.m_totalReceivedSize += pieceSize;
                return;
            }
            
            _LOG_ERR( "ERROR: unknown peer: " << (int)senderPublicKey[0] );
        }

        if ( auto drive = getDrive( transactionHash ); drive )
        {
            if ( isPeerReplicator( *drive, senderPublicKey ) )
            {
                //todo it is a late replicator
                return;
            }
        }
        
        _LOG( "unknown transactionHash: " << (int)transactionHash[0] );
        _LOG_WARN( "ERROR(3): unknown transactionHash: " << (int)transactionHash[0] );
    }


    // will be called when one replicator informs another about downloaded size by client
    virtual void acceptReceiptFromAnotherReplicator( const std::array<uint8_t,32>&  downloadChannelId,
                                                     const std::array<uint8_t,32>&  clientPublicKey,
                                                     const std::array<uint8_t,32>&  replicatorPublicKey,
                                                     uint64_t                       downloadedSize,
                                                     const std::array<uint8_t,64>&  signature ) override
    {
        DBG_MAIN_THREAD
        
        acceptReceipt(downloadChannelId,
                      clientPublicKey,
                      replicatorPublicKey,
                      downloadedSize,
                      signature);
    }
    
    void removeChannelInfo( const std::array<uint8_t,32>& channelId )
    {
        DBG_MAIN_THREAD

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

    bool verifyMutableItem( const std::vector<char>& value,
                            const int64_t& seq,
                            const std::string& salt,
                            const std::array<uint8_t,32>& pk,
                            const std::array<uint8_t,64>& sig ) override
    {
        return crypto::Verify(Key{pk},
                       {
                               utils::RawBuffer{reinterpret_cast<const uint8_t *>(value.data()), value.size()},
                               utils::RawBuffer{reinterpret_cast<const uint8_t *>(&seq), sizeof(int64_t)},
                               utils::RawBuffer{reinterpret_cast<const uint8_t *>(salt.data()), salt.size()}
                       },
                       reinterpret_cast<const Signature &>(sig));
    }

   void signMutableItem( const std::vector<char>& value,
                         const int64_t& seq,
                         const std::string& salt,
                         std::array<uint8_t,64>& sig ) override
   {
       DBG_MAIN_THREAD

       Signature signature;
       crypto::Sign(m_keyPair,
                    {
                        utils::RawBuffer{reinterpret_cast<const uint8_t *>(value.data()), value.size()},
                        utils::RawBuffer{reinterpret_cast<const uint8_t *>(&seq), sizeof(int64_t)},
                        utils::RawBuffer{reinterpret_cast<const uint8_t *>(salt.data()), salt.size()}
                    },
                    reinterpret_cast<Signature &>(sig));
   }

    bool acceptReceipt(const std::array<uint8_t, 32> &downloadChannelId,
                       const std::array<uint8_t, 32> &clientPublicKey,
                       const std::array<uint8_t, 32> &replicatorPublicKey,
                       uint64_t downloadedSize,
                       const std::array<uint8_t, 64> &signature) override
    {
        if ( !verifyReceipt(downloadChannelId,
                            clientPublicKey,
                            replicatorPublicKey,
                            downloadedSize,
                            signature))
        {
            return false;
        }

        auto& channelInfo = m_downloadChannelMap[downloadChannelId];
        auto& replicatorUploadMap = channelInfo.m_replicatorUploadMap;
        if (  !replicatorUploadMap.contains(replicatorPublicKey) ) {
            replicatorUploadMap[replicatorPublicKey] = { 0 };
        }
        auto& replicatorInfo = replicatorUploadMap[replicatorPublicKey];
        uint64_t lastAcceptedUploadSize = replicatorInfo.m_uploadedSize;
        channelInfo.m_totalReceiptsSize = channelInfo.m_totalReceiptsSize - lastAcceptedUploadSize + downloadedSize;
        replicatorInfo.m_uploadedSize = downloadedSize;

        return true;
    }
    
    bool verifyReceipt(  const std::array<uint8_t,32>&  downloadChannelId,
                         const std::array<uint8_t,32>&  clientPublicKey,
                         const std::array<uint8_t,32>&  replicatorPublicKey,
                         uint64_t                       downloadedSize,
                         const std::array<uint8_t,64>&  signature )
    {
        DBG_MAIN_THREAD
        if ( !crypto::Verify( clientPublicKey,
                               {
                                    utils::RawBuffer{downloadChannelId},
                                    utils::RawBuffer{clientPublicKey},
                                    utils::RawBuffer{replicatorPublicKey},
                                    utils::RawBuffer{(const uint8_t*)&downloadedSize,8}
                               },
                               reinterpret_cast<const Signature&>(signature) ))
        {
            _LOG_WARN( dbgOurPeerName() << ": verifyReceipt: invalid signature: " << int(downloadChannelId[0]) << " " << int(replicatorPublicKey[0]) );
            return false;
        }

        auto it = m_downloadChannelMap.find( downloadChannelId );
        if ( it == m_downloadChannelMap.end() )
        {
            _LOG_WARN( dbgOurPeerName() << ": verifyReceipt: unknown channelId (maybe we are late): " << int(downloadChannelId[0]) << " " << int(replicatorPublicKey[0]) );
            return false;
        }
        // check client key
        if ( !it->second.m_isModifyTx )
        {
            const auto& v = it->second.m_clients;
            if ( std::find_if( v.begin(), v.end(), [&clientPublicKey](const auto& element)
            { return element == clientPublicKey; } ) == v.end() )
            {
                _LOG_WARN( "verifyReceipt: bad client key; it is ignored" );
                return false;
            }
        }

        auto replicatorIt = it->second.m_replicatorUploadMap.find( replicatorPublicKey );

        uint64_t lastAcceptedUploadSize;
        if ( replicatorIt == it->second.m_replicatorUploadMap.end() )
        {
            const auto& v = it->second.m_replicatorsList2;
            if (std::find(v.begin(), v.end(), replicatorPublicKey) == v.end())
            {
                _LOG_WARN("verifyReceipt: bad replicator key; it is ignored");
                return false;
            }
            lastAcceptedUploadSize = 0;
        }
        else {
            lastAcceptedUploadSize = replicatorIt->second.m_uploadedSize;
        }
        if ( lastAcceptedUploadSize >= downloadedSize )
        {
            _LOG_WARN("verifyReceipt: old receipt; it is ignored");
            return false;
        }
        if ( it->second.m_totalReceiptsSize - lastAcceptedUploadSize + downloadedSize > it->second.m_prepaidDownloadSize )
        {
            _LOG_WARN("verifyReceipt: attempt to download more than prepaid; it is ignored ") ;
            return false;
        }
        if ( downloadedSize >= it ->second.m_uploadedSize + m_advancePaymentLimit )
        {
            _LOG_WARN("verifyReceipt: attempt to prepay too much");
            return false;
        }
        return true;
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
    
    std::shared_ptr<sirius::drive::FlatDrive> getDrive( const Key driveKey )
    {
        std::shared_lock<std::shared_mutex> lock(m_driveMutex);
        if ( auto it = m_driveMap.find(driveKey); it != m_driveMap.end() )
        {
            return it->second;
        }
        return {};
    }

    //--------------------------------------------------------------------------------------------------
    void saveRestartData( std::string outputFile, const std::string data )
    {
        try
        {
            {
                std::ofstream fStream( outputFile +".tmp", std::ios::binary );
                fStream << data;
            }
            std::error_code err;
            fs::remove( outputFile, err );
            fs::rename( outputFile +".tmp", outputFile , err );
        }
        catch(...)
        {
            _LOG_WARN( "saveRestartData: cannot save" );
        }
    }
    
    bool loadRestartData( std::string outputFile, std::string& data )
    {
        std::error_code err;
        
        if ( fs::exists( outputFile,err) )
        {
            std::ifstream ifStream( outputFile, std::ios::binary );
            if ( ifStream.is_open() )
            {
                std::ostringstream os;
                os << ifStream.rdbuf();
                data = os.str();
                return true;
            }
        }
        
        if ( fs::exists( outputFile +".tmp", err ) )
        {
            std::ifstream ifStream( outputFile +".tmp", std::ios::binary );
            if ( ifStream.is_open() )
            {
                std::ostringstream os;
                os << ifStream.rdbuf();
                data = os.str();
                return true;
            }
        }
        
        return false;
    }

};

}

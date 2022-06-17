/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include "drive/Replicator.h"
#include "ReplicatorInt.h"
#include <sirius_drive/session_delegate.h>
#include "crypto/Signer.h"
#include "types.h"

#include <map>
#include <shared_mutex>
#include <iostream>
#include <fstream>
#include <sstream>

#include <boost/stacktrace.hpp>

#undef DBG_MAIN_THREAD
//#define DBG_MAIN_THREAD { assert( m_dbgThreadId == std::this_thread::get_id() ); }
#define DBG_MAIN_THREAD { _FUNC_ENTRY(); assert( m_dbgThreadId == std::this_thread::get_id() ); }

namespace sirius::drive {

namespace fs = std::filesystem;

//
// DownloadLimiter - it manages all user files at replicator side
//
class DownloadLimiter : public ReplicatorInt,
                        public lt::session_delegate,
                        public std::enable_shared_from_this<DownloadLimiter>
{
protected:
    std::shared_ptr<Session> m_session;

    // Replicator's keys
    const crypto::KeyPair& m_keyPair;

    ChannelMap          m_dnChannelMap; // will be saved only if not crashed
    ChannelMap          m_dnChannelMapBackup;
    ModifyDriveMap      m_modifyDriveMap;

    uint64_t            m_receiptLimit = 1024 * 1024;
    uint64_t            m_advancePaymentLimit = 50 * 1024 * 1024;

    std::string         m_dbgOurPeerName = "unset";
    
    // Drives
    std::map<Key, std::shared_ptr<FlatDrive>> m_driveMap;

    std::thread::id     m_dbgThreadId;

public:
    DownloadLimiter( const crypto::KeyPair& keyPair, const char* dbgOurPeerName ) : m_keyPair(keyPair), m_dbgOurPeerName(dbgOurPeerName)
    {
    }
    
    virtual void onTorrentDeleted( lt::torrent_handle handle ) override
    {
        m_session->onTorrentDeleted( handle );
    }

    
    void printReport( std::array<uint8_t,32> txHash )
    {
        boost::asio::post(m_session->lt_session().get_context(), [=,this]() mutable {
            
            _LOG( "printReport:" );
            if ( auto it = m_dnChannelMap.find( txHash ); it != m_dnChannelMap.end() )
            {
                for( auto& [key,value] : it->second.m_dnClientMap )
                {
                    _LOG( "client: " << int(key[0]) << " " << "requestedSize=" << value.m_requestedSize << "; uploadedSize=" << value.m_uploadedSize );
                }
                return;
            }

            _LOG( dbgOurPeerName() << "ERROR: printReport hash: " << (int)txHash[0] );
            assert(0);
        });
    }

    void onDisconnected( const std::array<uint8_t,32>&  txHash,
                         const std::array<uint8_t,32>&  peerKey,
                         int                            siriusFlags ) override
    {
        DBG_MAIN_THREAD
        
        //TODO++
        return;
        
        if ( ! (siriusFlags & lt::SiriusFlags::replicator_is_receiver) )
        {
            _LOG( "onDisconnected: " << dbgOurPeerName() << " from client: " << (int)peerKey[0] );
        }
        else
        {
            _LOG( "onDisconnected: " << dbgOurPeerName() << " from peer: " << (int)peerKey[0] );
            for( const auto& i : m_modifyDriveMap ) {
                _LOG( "m_modifyDriveMap: " << (int)i.first[0] << " " << (int)txHash[0]);
            }

            if ( const auto& it = m_modifyDriveMap.find( txHash ); it != m_modifyDriveMap.end() )
            {
                for( const auto& replicatorIt : it->second.m_modifyTrafficMap )
                {
                    _LOG( " m_receivedSize: " <<  replicatorIt.second.m_receivedSize << " from " << (int)replicatorIt.first[0] );
                }
                _LOG( " ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ " );
            }
        }
    }

    void dbgPrintTrafficDistribution( std::array<uint8_t,32> txHash ) override
    {
        boost::asio::post(m_session->lt_session().get_context(), [=,this]() mutable {
            DBG_MAIN_THREAD
              
            if ( const auto& it = m_modifyDriveMap.find( txHash ); it == m_modifyDriveMap.end() )
            {
                _LOG( "\nTrafficDistribution NOT FOUND: " << dbgOurPeerName() << " (" << (int)publicKey()[0] << ")" );
            }
            else
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
                }
            }
      });
    }
    
    virtual ModifyTrafficInfo getMyDownloadOpinion( const Hash256& txHash ) const override
    {
        DBG_MAIN_THREAD

        if ( const auto it = m_modifyDriveMap.find( txHash.array() ); it != m_modifyDriveMap.end() )
        {
            return it->second;
        }
        _LOG_ERR( "getMyDownloadOpinion: unknown modify transaction hash: " << txHash );
        
        return {};
    }


    bool checkDownloadLimit( const std::array<uint8_t,64>& /*signature*/,
                             const std::array<uint8_t,32>& downloadChannelId,
                             uint64_t /*downloadedSize*/) override
    {
        DBG_MAIN_THREAD
        
        if ( m_session->isEnding() )
        {
            return false;
        }

        auto it = m_dnChannelMap.find( downloadChannelId );

        if ( it == m_dnChannelMap.end() )
        {
            _LOG ("Check Download Limit:  No Such a Download Channel");
            return false;
        }

        if ( it->second.m_isSyncronizing )
        {
            _LOG ("Check Download Limit: channel is syncronizing");
            return false;
        }

// (???+) TODO!!!
//        auto replicatorIt = it->second.m_replicatorUploadMap.find( publicKey() );
//
//        if( replicatorIt == it->second.m_replicatorUploadMap.end() )
//        {
//            _LOG ("Check Download Limit:  No Such a Replicator");
//            return false;
//        }
//
//        if ( it->second.m_myUploadedSize > replicatorIt->second.m_uploadedSize + m_receiptLimit )
//        {
//            _LOG ("Check Download Limit:  Receipts Size Exceeded");
//            return false;
//        }
        return true;
    }

    uint8_t getUploadedSize( const std::array<uint8_t,32>& downloadChannelId ) override
    {
        DBG_MAIN_THREAD

        uint64_t uploadedSize = 0;
        if ( auto it = m_dnChannelMap.find( downloadChannelId ); it != m_dnChannelMap.end() )
        {
            for( auto& cell: it->second.m_dnClientMap )
            {
                uploadedSize += cell.second.m_uploadedSize;
            }
        }
        return uploadedSize;
    }

    void addChannelInfo( const std::array<uint8_t,32>&  channelId,
                         uint64_t                       prepaidDownloadSize,
                         const Key&                     driveKey,
                         const ReplicatorList&          replicatorsList,
                         const std::vector<std::array<uint8_t,32>>&  clients,
                         bool                           willBeSyncronized )
    {
        DBG_MAIN_THREAD

        if ( auto it = m_dnChannelMap.find(channelId); it != m_dnChannelMap.end() ) {
			_LOG_ERR( "Attempt To Add Already Existing Drive " << int(channelId[0]) );
			return;
		}

//            Possible problem with receipts
//            /// Change client list of existing clients
//            if ( !clients.empty() )
//            {
//                it->second.m_clients = clients;
//            }

        DownloadChannelInfo dnChannelInfo {
            willBeSyncronized, prepaidDownloadSize, 0, {}, driveKey.array(), replicatorsList, {}, {} };
        
        // Every client has its own cell
        for( auto& clientKey : clients )
        {
            dnChannelInfo.m_dnClientMap.insert( { clientKey, {} } );
        }
        
        // We prepare upload map that contains information about how much each Replicator has uploaded
        for( const auto& it : replicatorsList )
        {
            if ( it.array() != publicKey() )
                dnChannelInfo.m_replicatorUploadMap.insert( { it, {}} );
        }
        
        m_dnChannelMap.insert( { channelId, std::move(dnChannelInfo) } );

        if ( auto backupIt = m_dnChannelMapBackup.find(channelId); backupIt != m_dnChannelMapBackup.end() )
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
                /// !!! (???)
//                _LOG_ERR( "addChannelInfo: channel size decreased" );
            }

            auto it = m_dnChannelMap.find(channelId);

            //(+++)??? restore backup info
            it->second.m_dnClientMap          = backupIt->second.m_dnClientMap;
            it->second.m_replicatorUploadMap  = backupIt->second.m_replicatorUploadMap;
            it->second.m_downloadOpinionMap   = backupIt->second.m_downloadOpinionMap;

            m_dnChannelMapBackup.erase(backupIt);
        }
    }

    void increaseChannelSize( const std::array<uint8_t,32>&  channelId,
							  uint64_t                       prepaidDownloadSize)
	{
    	auto it = m_dnChannelMap.find(channelId);

		if (it == m_dnChannelMap.end()) {
			_LOG_ERR( "Attempt To Increase Size Of Not Existing Download Channel " << int(channelId[0]) );
			return;
		}

		it->second.m_prepaidDownloadSize += prepaidDownloadSize;
	}

    bool addModifyTrafficInfo( const Key&                 modifyTransactionHash,
                               const Key&                 driveKey,
                               uint64_t                   dataSize,
                               const Key&                 clientPublicKey,
                               const std::vector<Key>&    replicatorsList )
    {
        DBG_MAIN_THREAD

        _LOG( "add modify drive info " << modifyTransactionHash);

        auto driveMapIt = m_modifyDriveMap.lower_bound(modifyTransactionHash.array());

        if (driveMapIt != m_modifyDriveMap.end() && driveMapIt->first == modifyTransactionHash.array())
        {
            // already exists
            return false;
        }

        ModifyTrafficMap trafficMap;
        trafficMap.insert( { clientPublicKey.array(), {0,0}} );
        
        for( const auto& it : replicatorsList )
        {
            if ( it.array() != publicKey() )
            {
                //_LOG( dbgOurPeerName() << " pubKey: " << (int)it.m_publicKey.array()[0] );
                trafficMap.insert( { it.array(), {0,0}} );
            }
        }
        
        m_modifyDriveMap.insert(driveMapIt, {modifyTransactionHash.array(), ModifyTrafficInfo{ driveKey.array(), dataSize, trafficMap, 0 }});

        return true;
    }
    
    void removeModifyDriveInfo( const std::array<uint8_t,32>& modifyTransactionHash ) override
    {
        DBG_MAIN_THREAD

        _LOG( "remove modify drive info " << Hash256{modifyTransactionHash});

        m_modifyDriveMap.erase(modifyTransactionHash);
    }

    bool acceptClientConnection( const std::array<uint8_t,32>&  channelId,
                                 const std::array<uint8_t,32>&  peerKey ) override
    {
        DBG_MAIN_THREAD
        
        if ( m_session->isEnding() )
        {
            return false;
        }
        
        if ( auto it = m_dnChannelMap.find( channelId ); it != m_dnChannelMap.end() )
        {
            const auto& clientMap = it->second.m_dnClientMap;
            if ( clientMap.find(peerKey) == clientMap.end() ) {
                return false;
            }
            if ( it->second.m_totalReceiptsSize < it->second.m_prepaidDownloadSize )
            {
                return true;
            }
            else
            {
                _LOG_WARN( "Failed: it->second.m_totalReceiptsSize < it->second.m_prepaidDownloadSize: "
                          << it->second.m_totalReceiptsSize << "  " << it->second.m_prepaidDownloadSize )
                return false;
            }
        }
        
        _LOG_WARN( "Unknown channelId: " << Key(channelId) << " from_peer:" << Key(peerKey) )
        return false;
    }

    bool acceptReplicatorConnection( const std::array<uint8_t,32>&  driveKey,
                                     const std::array<uint8_t,32>&  peerKey ) override
    {
        DBG_MAIN_THREAD
        
        if ( m_session->isEnding() )
        {
            return false;
        }
        
        // Replicator downloads from another replicator
        if ( auto driveIt = m_driveMap.find( Key(driveKey) ); driveIt != m_driveMap.end() )
        {
            if ( driveIt->second->acceptConnectionFromReplicator( peerKey ) )
            {
                return true;
            }
        }

        _LOG_WARN( "Unknown driveKey: " << Key(driveKey) << " from_peer:" << Key(peerKey) )
        return false;
    }

    void onPieceRequestWrite( const std::array<uint8_t,32>&  txHash,
                              const std::array<uint8_t,32>&  receiverPublicKey,
                              uint64_t                       pieceSize ) override
    {
// Replicator nothing does
//        DBG_MAIN_THREAD
//
//        if ( auto it = m_dnChannelMap.find( txHash ); it != m_dnChannelMap.end() )
//        {
//            it->second.m_requestedSize += pieceSize;
//            return;
//        }
    }
    
    bool onPieceRequestReceivedFromReplicator( const std::array<uint8_t,32>&  modifyTx,
                                               const std::array<uint8_t,32>&  receiverPublicKey,
                                               uint64_t                       pieceSize ) override
    {
        DBG_MAIN_THREAD
        
        if ( m_session->isEnding() )
        {
            return false;
        }


        if ( auto it = m_modifyDriveMap.find( modifyTx ); it != m_modifyDriveMap.end() )
        {
            if ( auto peerIt = it->second.m_modifyTrafficMap.find(receiverPublicKey);  peerIt != it->second.m_modifyTrafficMap.end() )
            {
                peerIt->second.m_requestedSize += pieceSize;
            }
        }
        return true;
    }
    
    bool onPieceRequestReceivedFromClient( const std::array<uint8_t,32>&      transactionHash,
                                           const std::array<uint8_t,32>&      receiverPublicKey,
                                           uint64_t                           pieceSize ) override
    {
        //(???+++)
        //TODO
        return true;
    }

    void onPieceSent( const std::array<uint8_t,32>&  txHash,
                      const std::array<uint8_t,32>&  receiverPublicKey,
                      uint64_t                       pieceSize ) override
    {
        DBG_MAIN_THREAD
        
        if ( m_session->isEnding() )
        {
            return;
        }


        // Maybe this piece was sent to client (during data download)
        if ( auto it = m_dnChannelMap.find( txHash ); it != m_dnChannelMap.end() )
        {
            if ( auto it2 = it->second.m_dnClientMap.find( receiverPublicKey ); it2 != it->second.m_dnClientMap.end() )
            {
                it2->second.m_uploadedSize += pieceSize;
            }
            else
            {
                _ASSERT( "unknown receiverPublicKey" );
            }
            return;
        }

        _LOG_WARN( "unknown txHash: " << (int)txHash[0] );
    }
    
    void onPieceReceived( const std::array<uint8_t,32>&  modifyTx,
                          const std::array<uint8_t,32>&  senderPublicKey,
                          uint64_t                       pieceSize ) override
    {
        DBG_MAIN_THREAD
        
        if ( m_session->isEnding() )
        {
            return;
        }

        if ( auto it = m_modifyDriveMap.find( modifyTx ); it != m_modifyDriveMap.end() )
        {
            if ( auto peerIt = it->second.m_modifyTrafficMap.find(senderPublicKey);  peerIt != it->second.m_modifyTrafficMap.end() )
            {
                peerIt->second.m_receivedSize  += pieceSize;
                it->second.m_totalReceivedSize += pieceSize;
                return;
            }
            
            if ( m_dnChannelMap.find( modifyTx ) != m_dnChannelMap.end() )
            {
                _LOG( "received piece from viewer/client: " << Key(modifyTx) );
            }
            else
            {
                _LOG( "received piece from viewer/client: " << Key(modifyTx) );
                _LOG_WARN( "ERROR: unknown peer: " << (int)senderPublicKey[0] );
            }
            return;
        }

        _LOG( "unknown txHash: " << Key(modifyTx) );
        _LOG_WARN( "ERROR(3): unknown txHash: " << Key(modifyTx) );
    }


    // will be called when one replicator informs another about downloaded size by client
    virtual bool acceptReceiptFromAnotherReplicator( const RcptMessage& message ) override
    {
        DBG_MAIN_THREAD
        
        if ( m_session->isEnding() )
        {
            return false;
        }
        
        return acceptReceiptImpl( message, true );
    }
    
    void removeChannelInfo( const ChannelId& channelId )
    {
        DBG_MAIN_THREAD

        m_dnChannelMap.erase( channelId );
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

        crypto::Sign( m_keyPair,
                {
                    utils::RawBuffer{reinterpret_cast<const uint8_t *>(value.data()), value.size()},
                    utils::RawBuffer{reinterpret_cast<const uint8_t *>(&seq), sizeof(int64_t)},
                    utils::RawBuffer{reinterpret_cast<const uint8_t *>(salt.data()), salt.size()}
                },
                reinterpret_cast<Signature &>(sig) );
    }

    bool acceptReceipt( const std::array<uint8_t, 32>& downloadChannelId,
                        const std::array<uint8_t, 32>& clientPublicKey,
                        const std::array<uint8_t, 32>& replicatorKey,
                        uint64_t                       downloadedSize,
                        const std::array<uint8_t, 64>& signature) override
    {
        if ( m_session->isEnding() )
        {
            return false;
        }

        return acceptReceiptImpl(
                    RcptMessage( ChannelId(downloadChannelId),
                                 ClientKey(clientPublicKey),
                                 ReplicatorKey(replicatorKey),
                                 downloadedSize,
                                 signature ), false );
    }
    
    bool acceptReceiptImpl( const RcptMessage& msg, bool fromAnotherReplicator )
    {
        // Get channel info
        auto channelInfoIt = m_dnChannelMap.find( msg.channelId() );
        if ( channelInfoIt == m_dnChannelMap.end() )
        {
            _LOG_WARN( dbgOurPeerName() << "unknown channelId (maybe we are late): " << int(msg.channelId()[0]) << " " << int(msg.replicatorKey()[0]) );
            return false;
        }

        // Check client key
        //
        const auto& clientMap = channelInfoIt->second.m_dnClientMap;
        if ( clientMap.find( msg.clientKey() ) == clientMap.end() )
        {
            _LOG_WARN( "verifyReceipt: bad client key; it is ignored" );
            return false;
        }

        //
        // Check sign
        //
        if ( ! crypto::Verify( msg.clientKey(),
                               {
                                    utils::RawBuffer{msg.channelId()},
                                    utils::RawBuffer{msg.clientKey()},
                                    utils::RawBuffer{msg.replicatorKey()},
                                    utils::RawBuffer{(const uint8_t*)msg.downloadedSizePtr(),8}
                               },
                               reinterpret_cast<const Signature&>(msg.signature()) ))
        {
            _LOG( "msg.channelId() " << msg.channelId() )
            _LOG( "msg.clientKey() " << msg.clientKey() )
            _LOG( "msg.replicatorKey() " << msg.replicatorKey() )
            _LOG( "downloadedSize: " << *msg.downloadedSizePtr() )
            _LOG( "msg.signature() " << int(msg.signature()[0]) )

            _LOG_WARN( dbgOurPeerName() << ": verifyReceipt: invalid signature: " << int(msg.channelId()[0]) << " " << int(msg.replicatorKey()[0]) )
            return false;
        }

        auto& channelInfo = channelInfoIt->second;

        bool isAccepted = acceptUploadSize( msg, channelInfo );
        
        if ( isAccepted && fromAnotherReplicator )
        {
            if ( fromAnotherReplicator )
            {
                //
                // Select 4 replicators and forward them the receipt
                //
                auto keyPointersSize = channelInfoIt->second.m_dnReplicatorShard.size() - 1;
                Key* keyPointers[keyPointersSize];
                auto keyIt = keyPointers;
                for( auto replicatorIt = channelInfoIt->second.m_dnReplicatorShard.begin(); replicatorIt != channelInfoIt->second.m_dnReplicatorShard.end(); replicatorIt++ )
                {
                    if ( *replicatorIt != m_keyPair.publicKey().array() )
                    {
                        *keyIt = &(*replicatorIt);
                        keyIt++;
                    }
                }
                
                auto forwardSize = keyPointersSize;
                while( forwardSize > 4 )
                {
                    int randIndex = rand() % forwardSize;
                    if ( keyPointers[randIndex] != nullptr )
                    {
                        keyPointers[randIndex] = nullptr;
                        forwardSize--;
                    }
                }
                
                for( size_t i=0; i<keyPointersSize; i++ )
                {
                    if ( keyPointers[i] != nullptr )
                    {
                        sendMessage( "rcpt", keyPointers[i]->array(), msg );
                    }
                }
            }
        }
        
        return isAccepted;
    }
    
    bool acceptUploadSize( const RcptMessage& msg, DownloadChannelInfo& channelInfo )
    {
        DBG_MAIN_THREAD

        auto replicatorInfoIt = channelInfo.m_replicatorUploadMap.lower_bound( msg.replicatorKey() );
        
        if ( replicatorInfoIt == channelInfo.m_replicatorUploadMap.end() )
        {
            // It is first receipt for this replicator
            // Check that it exists in our 'shard'

            //(?) It must be the list of ALL replicators that
//            const auto& v = channelInfo.m_dnReplicatorShard;
//            if ( std::find( v.begin(), v.end(), msg.replicatorKey() ) == v.end() )
//            {
//                _LOG_WARN("bad replicator key; it is ignored");
//                return false;
//            }
            
            replicatorInfoIt = channelInfo.m_replicatorUploadMap.insert( replicatorInfoIt, {msg.replicatorKey(),{}} );
        }
        
        auto lastAcceptedUploadSize = replicatorInfoIt->second.uploadedSize( msg.clientKey() );
        
        //_LOG("lastAcceptedUploadSize: " << int(msg.replicatorKey()[0]) << " " << lastAcceptedUploadSize << " " << msg.downloadedSize() );
        if ( msg.downloadedSize() <= lastAcceptedUploadSize  )
        {
            _LOG("old receipt; it is ignored");
            return false;
        }
        
        if ( channelInfo.m_totalReceiptsSize - lastAcceptedUploadSize + msg.downloadedSize() > channelInfo.m_prepaidDownloadSize )
        {
            _LOG("attempt to download more than prepaid; it is ignored ");
            return false;
        }

        if ( msg.replicatorKey() == m_keyPair.publicKey().array() )
        {
            if ( auto clientSizesIt = channelInfo.m_dnClientMap.find( msg.clientKey() ); clientSizesIt != channelInfo.m_dnClientMap.end() )
            {
                auto requestedSize = msg.downloadedSize() - clientSizesIt->second.m_uploadedSize;
                if ( requestedSize >= m_advancePaymentLimit )
                {
                    // The client is forbidden to prepay too much in order to avoid an attack
                    _LOG_WARN("attempt to hand over large receipt");
                    return false;
                }
            }
        }
        
        channelInfo.m_totalReceiptsSize += msg.downloadedSize() - lastAcceptedUploadSize;
        replicatorInfoIt->second.acceptReceipt( msg.clientKey(), msg.downloadedSize() );
        
        auto clientReceiptIt = channelInfo.m_clientReceiptMap.lower_bound( msg.clientKey() );
        
        if ( clientReceiptIt == channelInfo.m_clientReceiptMap.end() )
        {
            ClientReceipts receipts;
            channelInfo.m_clientReceiptMap.insert( clientReceiptIt, { msg.clientKey(), receipts } );
        }
        else
        {
            auto replicatorIt = clientReceiptIt->second.lower_bound( msg.clientKey() );
            if ( replicatorIt == clientReceiptIt->second.end() )
            {
                clientReceiptIt->second.insert( replicatorIt, { msg.replicatorKey(), msg } );
            }
            else
            {
                //todo+++ (???++++) _ASSERT( replicatorIt->second.downloadedSize() <= msg.downloadedSize() );
                replicatorIt->second = std::move(msg);
            }
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

    const Key& dbgReplicatorKey() const override
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

    uint64_t receivedSize( const std::array<uint8_t,32>&  peerKey ) override
    {
        DBG_MAIN_THREAD
        return 0;
    }

    uint64_t requestedSize( const std::array<uint8_t,32>&  peerKey ) override
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
        DBG_MAIN_THREAD

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

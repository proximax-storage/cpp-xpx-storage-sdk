/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include "drive/Replicator.h"
#include "ReplicatorInt.h"
#include "drive/FlatDrive.h"
#include <sirius_drive/session_delegate.h>
#include "crypto/Signer.h"
#include "types.h"
#include "drive/Utils.h"

#include <map>
#include <shared_mutex>
#include <iostream>
#include <fstream>
#include <sstream>

#include <boost/stacktrace.hpp>

#undef DBG_MAIN_THREAD
//#define DBG_MAIN_THREAD { assert( m_dbgThreadId == std::this_thread::get_id() ); }
#define DBG_MAIN_THREAD _FUNC_ENTRY; assert( m_dbgThreadId == std::this_thread::get_id() );

namespace sirius::drive {

namespace fs = std::filesystem;

//
// DownloadLimiter - it manages all user files at replicator side
//
class DownloadLimiter : public ReplicatorInt,
                        public lt::session_delegate
{
protected:
    std::shared_ptr<Session> m_session;

    // Replicator's keys
    const crypto::KeyPair& m_keyPair;

    ChannelMap          m_dnChannelMap; // will be saved only if not crashed
    ChannelMap          m_dnChannelMapBackup;

    uint64_t            m_receiptLimit = 1024 * 1024;
    uint64_t            m_advancePaymentLimit = 50 * 1024 * 1024;

    std::string         m_dbgOurPeerName = "unset";
    
    // Drives
    std::map<Key, std::shared_ptr<FlatDrive>> m_driveMap;

    std::thread::id     m_dbgThreadId;

    bool m_isDestructing = false;

public:
    DownloadLimiter( const crypto::KeyPair& keyPair, const std::string& dbgOurPeerName ) : m_keyPair(keyPair), m_dbgOurPeerName(dbgOurPeerName)
    {
    }
    
    virtual void onTorrentDeleted( lt::torrent_handle handle ) override
    {
        if (m_isDestructing) {
            return;
        }
        m_session->onTorrentDeleted( handle );
    }

    void onCacheFlushed( lt::torrent_handle handle ) override
    {
        if (m_isDestructing) {
            return;
        }
        m_session->onCacheFlushed( handle );
    }
    
    void printReport( std::array<uint8_t,32> txHash )
    {
        if (m_isDestructing) {
            return;
        }
        boost::asio::post(m_session->lt_session().get_context(), [=,this]() mutable {
            
            _LOG( "printReport:" );
            if ( auto it = m_dnChannelMap.find( txHash ); it != m_dnChannelMap.end() )
            {
                for( auto& [key,value] : it->second.m_sentClientMap )
                {
                    _LOG( "client: " << int(key[0]) << "; uploadedSize=" << value.m_sentSize );
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

        if (m_isDestructing) {
            return;
        }

        //TODO++
        return;
        
        if ( ! (siriusFlags & lt::SiriusFlags::replicator_is_receiver) )
        {
            _LOG( "onDisconnected: " << dbgOurPeerName() << " from client: " << (int)peerKey[0] );
        }
        else
        {
            _LOG( "onDisconnected: " << dbgOurPeerName() << " from peer: " << (int)peerKey[0] );
        }
    }

    void dbgPrintTrafficDistribution( std::array<uint8_t,32> txHash ) override
    {
        boost::asio::post(m_session->lt_session().get_context(), [=,this]() mutable {
            DBG_MAIN_THREAD

            if (m_isDestructing) {
                return;
            }

            for( auto& [key,drive] : m_driveMap )
            {
                bool isFinished;
                if ( const auto& it = drive->findModifyInfo( Hash256(txHash), isFinished ); it != nullptr )
                {
                    _LOG( "\nTrafficDistribution: " << dbgOurPeerName() << " (isFinished:" << isFinished << ")" );
                    for( const auto& replicatorIt : it->m_modifyTrafficMap )
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
                    return;
                }
            }
            
            _LOG( "\nTrafficDistribution NOT FOUND: " << dbgOurPeerName() << " (" << (int)publicKey()[0] << ")" );
      });
    }
    
    bool checkDownloadLimit( const std::array<uint8_t,32>& peerKey,
                             const std::array<uint8_t,32>& downloadChannelId,
                             uint64_t                      downloadedSize,
                             lt::errors::error_code_enum&  outErrorCode ) override
    {
        DBG_MAIN_THREAD

        if (m_isDestructing) {
            return false;
        }

        outErrorCode = lt::errors::no_error;

        if ( m_session->isEnding() )
        {
            return false;
        }

        auto it = m_dnChannelMap.find( downloadChannelId );

        if ( it == m_dnChannelMap.end() )
        {
            _LOG ("Check Download Limit:  No Such a Download Channel");
            outErrorCode = lt::errors::sirius_no_channel;
            return false;
        }

        if ( it->second.m_isSynchronizing )
        {
            _LOG ("Check Download Limit: channel is syncronizing");
            //???
            outErrorCode = lt::errors::sirius_no_channel;
            return false;
        }

        uint64_t sentSize = 0;
        auto clientIt = it->second.m_sentClientMap.find(peerKey);

        if ( clientIt != it->second.m_sentClientMap.end() )
        {
            sentSize = clientIt->second.m_sentSize;
        }

        uint64_t receiptSize = 0;

        auto replicatorIt = it->second.m_replicatorUploadRequestMap.find(publicKey());

        if ( replicatorIt != it->second.m_replicatorUploadRequestMap.end() )
        {
            receiptSize = replicatorIt->second.lastAcceptedReceiptSize(peerKey);
        }

        if ( sentSize > receiptSize + m_receiptLimit )
        {
            _LOG ("Check Download Limit:  Receipts Size Exceeded");
            outErrorCode = lt::errors::sirius_receipt_size_too_small;
            return false;
        }
        return true;
    }

//    uint8_t getUploadedSize( const std::array<uint8_t,32>& downloadChannelId ) override
//    {
//        DBG_MAIN_THREAD
//
//        uint64_t uploadedSize = 0;
//        if ( auto it = m_dnChannelMap.find( downloadChannelId ); it != m_dnChannelMap.end() )
//        {
//            for( auto& cell: it->second.m_sentClientMap )
//            {
//                uploadedSize += cell.second.m_sentSize;
//            }
//        }
//        return uploadedSize;
//    }

    void addChannelInfo( const std::array<uint8_t,32>&  channelId,
                         uint64_t                       prepaidDownloadSize,
                         const Key&                     driveKey,
                         const ReplicatorList&          replicatorsList,
                         const std::vector<std::array<uint8_t,32>>&  clients,
                         bool                           willBeSyncronized )
    {
        DBG_MAIN_THREAD

        if (m_isDestructing) {
            return;
        }

        if ( auto it = m_dnChannelMap.find(channelId); it != m_dnChannelMap.end() ) {
            _LOG( "???" );
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
            willBeSyncronized, prepaidDownloadSize, 0, {}, {}, driveKey.array(), replicatorsList, {} };
        
        // Every client has its own cell
        for( auto& clientKey : clients )
        {
            dnChannelInfo.m_sentClientMap.insert( { clientKey, {} } );
        }
        
        // We prepare upload request map
        for( const auto& it : replicatorsList )
        {
            if ( it.array() != publicKey() )
                dnChannelInfo.m_replicatorUploadRequestMap.insert( { it, {}} );
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
            it->second.m_sentClientMap          = backupIt->second.m_sentClientMap;
            it->second.m_replicatorUploadRequestMap  = backupIt->second.m_replicatorUploadRequestMap;
            it->second.m_downloadOpinionMap   = backupIt->second.m_downloadOpinionMap;

            m_dnChannelMapBackup.erase(backupIt);
        }
    }

    void increaseChannelSize( const std::array<uint8_t,32>&  channelId,
							  uint64_t                       prepaidDownloadSize)
	{
        if (m_isDestructing) {
            return;
        }

    	auto it = m_dnChannelMap.find(channelId);

		if (it == m_dnChannelMap.end()) {
			_LOG_ERR( "Attempt To Increase Size Of Not Existing Download Channel " << int(channelId[0]) );
			return;
		}

		it->second.m_prepaidDownloadSize += prepaidDownloadSize;
	}

    lt::connection_status acceptClientConnection( const std::array<uint8_t,32>&  channelId,
                                                  const std::array<uint8_t,32>&  peerKey,
                                                  const std::array<uint8_t,32>&  driveKey,
                                                  const std::array<uint8_t,32>&  fileHash,
                                                  lt::errors::error_code_enum&   outErrorCode ) override
    {
        DBG_MAIN_THREAD

        if (m_isDestructing) {
            return lt::connection_status::REJECTED;
        }

        outErrorCode = lt::errors::no_error;

        if ( m_session->isEnding() )
        {
            return lt::connection_status::REJECTED;
        }

        if ( auto driveIt = m_driveMap.find( Key(driveKey) ); driveIt != m_driveMap.end() )
        {
            if ( driveIt->second->acceptConnectionFromClient( peerKey, fileHash ) )
            {
                return lt::connection_status::UNLIMITED;
            }
        }

        if ( auto it = m_dnChannelMap.find( channelId ); it != m_dnChannelMap.end() )
        {
            const auto& clientMap = it->second.m_sentClientMap;
            if ( clientMap.find(peerKey) == clientMap.end() )
            {
                outErrorCode = lt::errors::sirius_no_client_in_channel;
                return lt::connection_status::REJECTED;
            }
            if ( it->second.m_totalReceiptsSize < it->second.m_prepaidDownloadSize )
            {
                return lt::connection_status::LIMITED;
            }
            else
            {
                _LOG_WARN( "Failed: it->second.m_totalReceiptsSize < it->second.m_prepaidDownloadSize: "
                          << it->second.m_totalReceiptsSize << "  " << it->second.m_prepaidDownloadSize )
                outErrorCode = lt::errors::sirius_channel_ran_out;
                return lt::connection_status::REJECTED;
            }
        }
        
        _LOG_WARN( "Unknown channelId: " << Key(channelId) << " from_peer:" << Key(peerKey) )
        outErrorCode = lt::errors::sirius_no_channel;
        return lt::connection_status::REJECTED;
    }

    lt::connection_status acceptReplicatorConnection( const std::array<uint8_t,32>&  driveKey,
                                     const std::array<uint8_t,32>&  peerKey ) override
    {
        DBG_MAIN_THREAD

        if (m_isDestructing) {
            return lt::connection_status::REJECTED;
        }

        if ( m_session->isEnding() )
        {
            return lt::connection_status::REJECTED;
        }
        
        // Replicator downloads from another replicator
        if ( auto driveIt = m_driveMap.find( Key(driveKey) ); driveIt != m_driveMap.end() )
        {
            if ( driveIt->second->acceptConnectionFromReplicator( peerKey ) )
            {
                return lt::connection_status::UNLIMITED;
            }
        }
		else
		{
			_LOG_WARN( "Unknown driveKey: " << Key(driveKey) << " from_peer:" << Key(peerKey) )
		}

		return lt::connection_status::REJECTED;
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
    
    bool onPieceRequestReceivedFromReplicator( const std::array<uint8_t,32>&  driveKey,
                                               const std::array<uint8_t,32>&  receiverPublicKey,
                                               uint64_t                       pieceSize ) override
    {
        DBG_MAIN_THREAD

        if (m_isDestructing) {
            return false;
        }

        if ( m_session->isEnding() )
        {
            return false;
        }

        if ( auto it = m_driveMap.find( driveKey ); it != m_driveMap.end() )
        {
            auto& modifyInfo = it->second->currentModifyInfo();
            modifyInfo.m_modifyTrafficMap[receiverPublicKey].m_requestedSize  += pieceSize;
        }
        return true;
    }

    bool onPieceRequestReceivedFromClient( const std::array<uint8_t,32>&      transactionHash,
                                           const std::array<uint8_t,32>&      receiverPublicKey,
                                           uint64_t                           pieceSize ) override
    {
        if (m_isDestructing) {
            return false;
        }
        //(???+++)
        //TODO
        return true;
    }

    void onPieceSent( const std::array<uint8_t,32>&  txHash,
                      const std::array<uint8_t,32>&  receiverPublicKey,
                      uint64_t                       pieceSize ) override
    {
        DBG_MAIN_THREAD

        if (m_isDestructing) {
            return;
        }

        if ( m_session->isEnding() )
        {
            return;
        }


        // Maybe this piece was sent to client (during data download)
        if ( auto it = m_dnChannelMap.find( txHash ); it != m_dnChannelMap.end() )
        {
            if ( auto it2 = it->second.m_sentClientMap.find( receiverPublicKey ); it2 != it->second.m_sentClientMap.end() )
            {
                it2->second.m_sentSize += pieceSize;
            }
            else
            {
                SIRIUS_ASSERT( "unknown receiverPublicKey" );
            }
            return;
        }

        _LOG_WARN( "unknown txHash: " << (int)txHash[0] );
    }
    
    void onPieceReceived( const std::array<uint8_t,32>&  driveKey,
                          const std::array<uint8_t,32>&  senderPublicKey,
                          uint64_t                       pieceSize ) override
    {
        DBG_MAIN_THREAD

        if (m_isDestructing) {
            return;
        }

        if ( m_session->isEnding() )
        {
            return;
        }

        if ( auto it = m_driveMap.find( driveKey ); it != m_driveMap.end() )
        {
            auto& modifyInfo = it->second->currentModifyInfo();
            modifyInfo.m_modifyTrafficMap[senderPublicKey].m_receivedSize  += pieceSize;
            return;
        }

        _LOG_WARN( "unknown driveKey: " << Key(driveKey) );
    }

    // will be called when one replicator informs another about downloaded size by client
    virtual void acceptReceiptFromAnotherReplicator( const RcptMessage& message ) override
    {
        DBG_MAIN_THREAD

        if (m_isDestructing) {
            return;
        }

        if ( m_session->isEnding() )
        {
            return;
        }
        
        bool unused;
        lt::errors::error_code_enum unused2;
        acceptReceiptImpl( message, true, unused, unused2 );
    }
    
    void removeChannelInfo( const ChannelId& channelId )
    {
        DBG_MAIN_THREAD

        if (m_isDestructing) {
            return;
        }

        m_dnChannelMap.erase( channelId );
    }

    bool isClient() const override { return false; }

    void signHandshake( const uint8_t* bytes, size_t size, std::array<uint8_t,64>& signature ) override
    {
        DBG_MAIN_THREAD

        if (m_isDestructing) {
            return;
        }

        crypto::Sign( m_keyPair, utils::RawBuffer{bytes,size}, reinterpret_cast<Signature&>(signature) );
        //_LOG( "SIGN HANDSHAKE: " << int(signature[0]) )
    }

    bool verifyHandshake( const uint8_t*                 bytes,
                          size_t                         size,
                          const std::array<uint8_t,32>&  publicKey,
                          const std::array<uint8_t,64>&  signature ) override
    {
        DBG_MAIN_THREAD

        if (m_isDestructing) {
            return false;
        }

        //_LOG( "verifyHandshake: " << int(signature[0]) )
        auto ok = crypto::Verify( publicKey, utils::RawBuffer{bytes,size}, signature );
        if ( !ok )
        {
            _LOG_WARN( "verifyHandshake: failed" );
        }
        return ok;
    }

    void signReceipt( const std::array<uint8_t,32>& downloadChannelId,
                      const std::array<uint8_t,32>& replicatorPublicKey,
                      uint64_t                      downloadedSize,
                      std::array<uint8_t,64>&       outSignature ) override
    {
        DBG_MAIN_THREAD

        if (m_isDestructing) {
            return;
        }

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

        if (m_isDestructing) {
            return false;
        }

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

        if (m_isDestructing) {
            return;
        }

        crypto::Sign( m_keyPair,
                {
                    utils::RawBuffer{reinterpret_cast<const uint8_t *>(value.data()), value.size()},
                    utils::RawBuffer{reinterpret_cast<const uint8_t *>(&seq), sizeof(int64_t)},
                    utils::RawBuffer{reinterpret_cast<const uint8_t *>(salt.data()), salt.size()}
                },
                reinterpret_cast<Signature &>(sig) );
    }
    
    const std::vector<uint8_t>* getLastClientReceipt( const std::array<uint8_t,32>&  downloadChannelId,
                                                      const std::array<uint8_t,32>&  clientPublicKey ) override
    {
        if (m_isDestructing) {
            return nullptr;
        }

        auto channelInfoIt = m_dnChannelMap.find( downloadChannelId );
        if ( channelInfoIt == m_dnChannelMap.end() )
        {
            return nullptr;
        }
        
        auto clientReceiptIt = channelInfoIt->second.m_clientReceiptMap.find( clientPublicKey );
        if ( clientReceiptIt == channelInfoIt->second.m_clientReceiptMap.end() )
        {
            return nullptr;
        }

        auto replicatorIt = clientReceiptIt->second.find( this->m_keyPair.publicKey().array() ); //???
        if ( replicatorIt == clientReceiptIt->second.end() )
        {
            return nullptr;
        }
        
        return &replicatorIt->second;
    }


    void acceptReceipt( const std::array<uint8_t, 32>& downloadChannelId,
                        const std::array<uint8_t, 32>& clientPublicKey,
                        uint64_t                       downloadedSize,
                        const std::array<uint8_t, 64>& signature,
                        bool&                          shouldBeDisconnected,
                        lt::errors::error_code_enum&   outErrorCode ) override
    {
        if (m_isDestructing) {
            outErrorCode = lt::errors::no_error;
            return;
        }

        if ( m_session->isEnding() )
        {
            outErrorCode = lt::errors::no_error;
            return;
        }
        
        acceptReceiptImpl(
                RcptMessage( ChannelId(downloadChannelId),
                             ClientKey(clientPublicKey),
                             m_keyPair.publicKey().array(),
                             downloadedSize,
                             signature ),
                false, shouldBeDisconnected, outErrorCode );
    }
    
    void acceptReceiptImpl( const                           RcptMessage& msg,
                            bool                            fromAnotherReplicator,
                            bool&                           shouldBeDisconnected,
                            lt::errors::error_code_enum&    outErrorCode )
    {
        if (m_isDestructing) {
            outErrorCode = lt::errors::no_error;
            return;
        }

        outErrorCode = lt::errors::no_error;

        // Get channel info
        auto channelInfoIt = m_dnChannelMap.find( msg.channelId() );
        if ( channelInfoIt == m_dnChannelMap.end() )
        {
            _LOG_WARN( dbgOurPeerName() << "unknown channelId (maybe we are late): " << int(msg.channelId()[0]) << " " << int(msg.replicatorKey()[0]) );
            shouldBeDisconnected = true;
            outErrorCode = lt::errors::sirius_no_channel;
            return;
        }

        // Check client key
        //
        const auto& clientMap = channelInfoIt->second.m_sentClientMap;
        if ( clientMap.find( msg.clientKey() ) == clientMap.end() )
        {
            _LOG_WARN( "verifyReceipt: bad client key; it is ignored" );
            shouldBeDisconnected = true;
            outErrorCode = lt::errors::sirius_no_client_in_channel;
            return;
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
            shouldBeDisconnected = true;
            outErrorCode = lt::errors::sirius_bad_signature;
            return;
        }

        auto& channelInfo = channelInfoIt->second;

        acceptUploadSize( msg, channelInfo, shouldBeDisconnected, outErrorCode );
        
        // Save receipt message
        //
        bool shouldBeForwared = false;
        {
            if ( auto clientReceiptIt = channelInfo.m_clientReceiptMap.find( msg.clientKey() );
                 clientReceiptIt != channelInfo.m_clientReceiptMap.end() )
            {
                if ( clientReceiptIt->second[msg.replicatorKey()].downloadedSize() < msg.downloadedSize() )
                {
                    clientReceiptIt->second[msg.replicatorKey()] = msg;
                    shouldBeForwared = true;
                }
            }
            else
            {
                auto [insertedIt, success] = channelInfo.m_clientReceiptMap.insert( { msg.clientKey(), ClientReceipts() } );
                insertedIt->second[msg.replicatorKey()] = msg;
                shouldBeForwared = true;
            }
        }

        if ( shouldBeForwared )
        {
            if ( msg.replicatorKey() == m_keyPair.publicKey().array() )
            {
                sendReceiptToOtherReplicators( msg.channelId().array(),
                                               msg.clientKey().array(),
                                               msg.downloadedSize(),
                                               msg.signature() );
            }
            else
            {
                //
                // Select 4 replicators and forward them the receipt
                //

                std::vector<ReplicatorKey> receivers;
                {
                    std::vector<ReplicatorKey> candidates;
                    for( const auto& key: channelInfoIt->second.m_dnReplicatorShard )
                    {
                        if ( key != m_keyPair.publicKey().array() )
                        {
                            candidates.emplace_back(key);
                        }
                    }

                    while ( !candidates.empty() && receivers.size() < 4 )
                    {
                        auto randIndex = rand() % candidates.size();
                        std::swap( candidates[randIndex], candidates.back() );
                        receivers.emplace_back(candidates.back());
                        candidates.pop_back();
                    }
                }

                for( const auto& key: receivers )
                {
                    sendMessage( "rcpt", key, msg );
                }
            }
        }
    }
    
    void acceptUploadSize( const RcptMessage&           msg,
                           DownloadChannelInfo&         channelInfo,
                           bool&                        shouldBeDisconnected,
                           lt::errors::error_code_enum& outErrorCode )
    {
        DBG_MAIN_THREAD

        if (m_isDestructing) {
            outErrorCode = lt::errors::no_error;
            return;
        }

        shouldBeDisconnected = false;

        auto replicatorInfoIt = channelInfo.m_replicatorUploadRequestMap.find( msg.replicatorKey() );
        
        if ( replicatorInfoIt == channelInfo.m_replicatorUploadRequestMap.end() )
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
            
            replicatorInfoIt = channelInfo.m_replicatorUploadRequestMap.insert( replicatorInfoIt, {msg.replicatorKey(),{}} );
        }
        
        replicatorInfoIt->second.tryFixReceiptSize( channelInfo.m_prepaidDownloadSize );

        auto lastReceiptSize = replicatorInfoIt->second.maxReceiptSize( msg.clientKey() );
        //_LOG( "*rcpt* lastReceiptSize: " << lastReceiptSize << " msg: " << msg.downloadedSize() )

        // skip old receipts
        if ( msg.downloadedSize() <= lastReceiptSize  )
        {
            return;
        }

        // Update 'm_totalReceiptsSize'
        channelInfo.m_totalReceiptsSize += msg.downloadedSize() - replicatorInfoIt->second.maxReceiptSize( msg.clientKey() );

        // Check prepaid channel size
        if ( channelInfo.m_totalReceiptsSize > channelInfo.m_prepaidDownloadSize )
        {
            outErrorCode = lt::errors::sirius_channel_ran_out;
            shouldBeDisconnected = true;
            replicatorInfoIt->second.saveNotAcceptedReceiptSize( msg.clientKey(), msg.downloadedSize() );
            return;
        }

        replicatorInfoIt->second.saveReceiptSize( msg.clientKey(), msg.downloadedSize() );
        return;
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

    uint64_t receivedSize( const std::array<uint8_t,32>&  transactionHash,
                           const std::array<uint8_t,32>&  peerKey ) override
    {
        DBG_MAIN_THREAD
        return 0;
    }

    uint64_t requestedSize( const std::array<uint8_t,32>&,
                            const std::array<uint8_t,32>& ) override
    {
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

        if (m_isDestructing) {
            return {};
        }

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

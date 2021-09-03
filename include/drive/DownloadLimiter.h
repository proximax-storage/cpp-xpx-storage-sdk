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

namespace sirius { namespace drive {

//
// DownloadLimiter - it manages all user files at replicator side
//
class DownloadLimiter : public Replicator,
                        public lt::session_delegate,
                        public std::enable_shared_from_this<DownloadLimiter>
{
protected:
    
    // It is used for calculation of total data size, downloaded by 'client'
    struct ReplicatorUploadInfo
    {
        // It is the size uploaded by another replicator
        uint64_t m_uploadedSize = 0;
    };
    using ReplicatorUploadMap = std::map<std::array<uint8_t,32>,ReplicatorUploadInfo>;

    struct DownloadChannelInfo
    {
        uint64_t                m_prepaidDownloadSize;
        uint64_t                m_requestedSize = 0;
        uint64_t                m_uploadedSize = 0;
        std::vector<const Key>  m_clients; //todo
        ReplicatorList          m_replicatorsList;
        ReplicatorUploadMap     m_replicatorUploadMap;
    };

    using ChannelMap        = std::map<std::array<uint8_t,32>, DownloadChannelInfo>;

    // It is used for mutual calculation of the replicators, when they download 'modify data'
    // (Note. Replicators could receive 'modify data' from client and from replicators, that already receives some piece)
    struct ModifyTrafficInfo
    {
        // It is the size received from another replicator or client
        uint64_t m_receivedSize = 0;
        
        // It is the size sent to another replicator
        uint64_t m_requestedSize = 0;
        uint64_t m_sentSize = 0;
    };
    using ModifyTrafficMap = std::map<std::array<uint8_t,32>,ModifyTrafficInfo>;

    struct ModifyDriveInfo
    {
        uint64_t                m_dataSize;
        uint64_t                m_downloadedSize = 0;
        ReplicatorList          m_replicatorsList;
        ModifyTrafficMap        m_modifyTrafficMap;
    };

    using ModifyDriveMap    = std::map<std::array<uint8_t,32>, ModifyDriveInfo>;

protected:
    
    std::shared_mutex   m_mutex;

    ChannelMap          m_channelMap;
    ModifyDriveMap      m_modifyDriveMap;

    const crypto::KeyPair& m_keyPair;

    uint64_t m_receiptLimit = 32*1024; //1024*1024;

    const char* m_dbgOurPeerName = "unset";

public:
    DownloadLimiter( const crypto::KeyPair& keyPair, const char* dbgOurPeerName ) : m_keyPair(keyPair), m_dbgOurPeerName(dbgOurPeerName)
    {
    }
    
    void printReport( const std::array<uint8_t,32>&  transactionHash )
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        
        if ( auto it = m_channelMap.find( transactionHash ); it != m_channelMap.end() )
        {
            _LOG( "requestedSize=" << it->second.m_requestedSize << "; uploadedSize=" << it->second.m_uploadedSize );
            return;
        }

        _LOG( dbgOurPeerName() << "ERROR: printReport hash: " << (int)transactionHash[0] );
        assert(0);
    }

    bool acceptConnection( const std::array<uint8_t,32>&  transactionHash,
                           const std::array<uint8_t,32>&  peerPublicKey ) override
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        
        if ( auto it = m_channelMap.find( transactionHash ); it != m_channelMap.end() )
        {
            return true;
        }
        //todo++
        _LOG( dbgOurPeerName() << " hash: " << (int)transactionHash[0] );
        assert(0);
        return false;
    }

    void onDisconnected( const std::array<uint8_t,32>&  transactionHash,
                         const std::array<uint8_t,32>&  peerPublicKey,
                         int                            siriusFlags ) override
    {
        //TODO++
        return;
        
        std::shared_lock<std::shared_mutex> lock(m_mutex);

        if ( !(siriusFlags & lt::sf_is_receiver) )
        {
//            _LOG( " :::::::::::::::::::::::::::::::::::: " );
            _LOG( "onDisconnected: " << dbgOurPeerName() << " from client: " << (int)peerPublicKey[0] );
//            if ( const auto& it = m_channelMap.find( transactionHash ); it != m_channelMap.end() )
//            {
//                _LOG( " requestedSize: " << it->second.m_requestedSize );
//                _LOG( " uploadedSize: " <<  it->second.m_uploadedSize );
//                for( const auto& replicatorIt : it->second.m_replicatorUploadMap )
//                {
//                    _LOG( " uploadedSize: " <<  replicatorIt.second.m_uploadedSize << " by " << (int)replicatorIt.first[0] );
//                }
//            }
//            _LOG( " .................................... " );
        }
        else
        {
            //_LOG( " :::::::::::::::::::::::::::::::::::: " );
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
        std::shared_lock<std::shared_mutex> lock(m_mutex);

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
    }

    bool checkDownloadLimit( const std::array<uint8_t,64>& /*signature*/,
                             const std::array<uint8_t,32>& downloadChannelId,
                            uint64_t                       downloadedSizeByClient ) override
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        
        if ( auto it = m_channelMap.find( downloadChannelId ); it != m_channelMap.end() )
        {
//            int delay = int(it->second.m_uploadedSize - downloadedSizeByClient);
//            static int _dbgMaxDelay = 0;
//            if ( _dbgMaxDelay < delay )
//                _dbgMaxDelay = delay;
//
//            LOG( dbgOurPeerName() << " delay: " << _dbgMaxDelay );
//            LOG( dbgOurPeerName() << " " << int(downloadChannelId[0]) << " %%%%%%% " << int(it->second.m_uploadedSize - downloadedSizeByClient) << " : " << it->second.m_uploadedSize << " "<< downloadedSizeByClient << "\n");

            if ( it->second.m_uploadedSize > downloadedSizeByClient + m_receiptLimit )
                return false;
            return true;
        }
        return false;
    }

    uint8_t getUploadedSize( const std::array<uint8_t,32>& downloadChannelId ) override
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);

        if ( auto it = m_channelMap.find( downloadChannelId ); it != m_channelMap.end() )
        {
            return it->second.m_uploadedSize;
        }
        return 0;
    }

    void addChannelInfo( const std::array<uint8_t,32>&  channelId,
                         uint64_t                       prepaidDownloadSize,
                         const ReplicatorList&          replicatorsList,
                         const std::vector<const Key>&  clients )
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);

        if ( auto it = m_channelMap.find(channelId); it != m_channelMap.end() )
        {
            if ( it->second.m_prepaidDownloadSize <= prepaidDownloadSize )
            {
                throw std::runtime_error( "addChannelInfo: invalid prepaidDownloadSize" );
            }
            it->second.m_prepaidDownloadSize = prepaidDownloadSize;

            if ( clients.size() > 0 )
            {
                //it->second.m_clients = clients; //TODO!!!
            }
            return;
        }

        ReplicatorUploadMap map;
        for( const auto& it : replicatorsList )
        {
            if ( it.m_publicKey.array() != publicKey() )
                map.insert( { it.m_publicKey.array(), {}} );
        }
        m_channelMap[channelId] = DownloadChannelInfo{ prepaidDownloadSize, 0, 0, clients, replicatorsList, std::move(map) };
    }

    void addModifyDriveInfo( const Key&             modifyTransactionHash,
                             uint64_t               dataSize,
                             const Key&             clientPublicKey,
                             const ReplicatorList&  replicatorsList )
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);

        ModifyTrafficMap trafficMap;
        trafficMap.insert( { clientPublicKey.array(), {0,0}} );
        
        std::vector<const Key> clients;
        for( const auto& it : replicatorsList )
        {
            if ( it.m_publicKey.array() != publicKey() )
            {
                //_LOG( dbgOurPeerName() << " pubKey: " << (int)it.m_publicKey.array()[0] );
                trafficMap.insert( { it.m_publicKey.array(), {0,0}} );
                clients.emplace_back( it.m_publicKey );
            }
        }
        
        m_modifyDriveMap[modifyTransactionHash.array()] = ModifyDriveInfo{ dataSize, 0, replicatorsList, trafficMap };

        // we need to add modifyTransactionHash into 'm_channelMap'
        // because replicators could download pieces from their neighbors
        //
        m_channelMap[modifyTransactionHash.array()] = DownloadChannelInfo{ dataSize, 0, 0, std::move(clients), replicatorsList, {} };
        
    }
    
    void removeModifyDriveInfo( const std::array<uint8_t,32>& modifyTransactionHash )
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_modifyDriveMap.erase(modifyTransactionHash);
    }

    void onPieceRequested( const std::array<uint8_t,32>&  transactionHash,
                           const std::array<uint8_t,32>&  receiverPublicKey,
                           uint64_t                       pieceSize ) override
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);

        if ( auto it = m_channelMap.find( transactionHash ); it != m_channelMap.end() )
        {
            it->second.m_requestedSize += pieceSize;
            return;
        }

        LOG_ERR( "ERROR: unknown transactionHash: " << (int)transactionHash[0] );
    }
    
    void onPieceRequestReceived( const std::array<uint8_t,32>&  transactionHash,
                                 const std::array<uint8_t,32>&  receiverPublicKey,
                                 uint64_t                       pieceSize ) override
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);

        if ( auto it = m_modifyDriveMap.find( transactionHash ); it != m_modifyDriveMap.end() )
        {
            if ( auto peerIt = it->second.m_modifyTrafficMap.find(receiverPublicKey);  peerIt != it->second.m_modifyTrafficMap.end() )
            {
                peerIt->second.m_requestedSize += pieceSize;
                return;
            }
            LOG_ERR( "ERROR: unknown peer: " << (int)receiverPublicKey[0] );
        }

        LOG_ERR( "ERROR: unknown transactionHash(onPieceRequestReceived): " << (int)transactionHash[0] );
    }

    void onPieceSent( const std::array<uint8_t,32>&  transactionHash,
                      const std::array<uint8_t,32>&  receiverPublicKey,
                      uint64_t                       pieceSize ) override
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);

        if ( auto it = m_channelMap.find( transactionHash ); it != m_channelMap.end() )
        {
            it->second.m_uploadedSize += pieceSize;
            //return;
        }

        if ( auto it = m_modifyDriveMap.find( transactionHash ); it != m_modifyDriveMap.end() )
        {
            if ( auto peerIt = it->second.m_modifyTrafficMap.find(receiverPublicKey);  peerIt != it->second.m_modifyTrafficMap.end() )
            {
                peerIt->second.m_sentSize += pieceSize;
                return;
            }
            LOG_ERR( "ERROR: unknown peer: " << (int)receiverPublicKey[0] );
        }

        LOG_ERR( "ERROR(2): unknown transactionHash: " << (int)transactionHash[0] );
    }

    void onPieceReceived( const std::array<uint8_t,32>&  transactionHash,
                          const std::array<uint8_t,32>&  senderPublicKey,
                          uint64_t                       pieceSize ) override
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);

        if ( auto it = m_modifyDriveMap.find( transactionHash ); it != m_modifyDriveMap.end() )
        {
            if ( auto peerIt = it->second.m_modifyTrafficMap.find(senderPublicKey);  peerIt != it->second.m_modifyTrafficMap.end() )
            {
                peerIt->second.m_receivedSize += pieceSize;
                return;
            }
            LOG_ERR( "ERROR: unknown peer: " << (int)senderPublicKey[0] );
        }

        LOG_ERR( "ERROR(3): unknown transactionHash: " << (int)transactionHash[0] );
    }


    // will be called when one replicator informs another about downloaded size by client
    virtual void acceptReceiptFromAnotherReplicator( const std::array<uint8_t,32>&  downloadChannelId,
                                                     const std::array<uint8_t,32>&  clientPublicKey,
                                                     uint64_t                       downloadedSize,
                                                     const std::array<uint8_t,64>&  signature ) override
    {
        // verify receipt
        if ( !verifyReceipt(  downloadChannelId,
                              clientPublicKey,
                              publicKey(),
                              downloadedSize,
                              signature ) )
        {
            //todo log error?
            std::cerr << "ERROR! Invalid receipt" << std::endl << std::flush;
            assert(0);

            return;
        }

        //todo accept
        return;
    }
    
//    void sendReceiptToOtherReplicators( const std::array<uint8_t,32>&  downloadChannelId,
//                                 const std::array<uint8_t,32>&  clientPublicKey,
//                                 uint64_t                       downloadedSize,
//                                 const std::array<uint8_t,64>&  signature ) override
//    {
//        // it should be implemented by DefaultReplicator
//    }

    void removeChannelInfo( const std::array<uint8_t,32>& channelId )
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_channelMap.erase( channelId );
    }

    bool isClient() const override { return false; }

    void signHandshake( const uint8_t* bytes, size_t size, std::array<uint8_t,64>& signature ) override
    {
        crypto::Sign( m_keyPair, utils::RawBuffer{bytes,size}, reinterpret_cast<Signature&>(signature) );
    }

    bool verifyHandshake( const uint8_t*                 bytes,
                          size_t                         size,
                          const std::array<uint8_t,32>&  publicKey,
                          const std::array<uint8_t,64>&  signature ) override
    {
        return crypto::Verify( publicKey, utils::RawBuffer{bytes,size}, signature );
    }
    
    void signReceipt( const std::array<uint8_t,32>& downloadChannelId,
                      const std::array<uint8_t,32>& replicatorPublicKey,
                      uint64_t                      downloadedSize,
                      std::array<uint8_t,64>&       outSignature ) override
    {
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
//todo++
        LOG( "+SSS " << dbgOurPeerName() << " " << int(downloadChannelId[0]) << " " << (int)clientPublicKey[0] << " " << (int) replicatorPublicKey[0] << " " << downloadedSize );
//
        LOG( "+SSS: " << int(signature[0]) );

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

//    void setStartReceivedSize( uint64_t /*downloadedSize*/ ) override
//    {
//    }

    uint64_t receivedSize( const std::array<uint8_t,32>&  peerPublicKey ) override
    {
        return 0;
    }

    uint64_t requestedSize( const std::array<uint8_t,32>&  peerPublicKey ) override
    {
        return 0;
    }

    const char* dbgOurPeerName() override
    {
        return m_dbgOurPeerName;
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

}}

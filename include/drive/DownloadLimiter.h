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

namespace sirius { namespace drive {

//
// DownloadLimiter - it manages all user files at replicator side
//
class DownloadLimiter : public Replicator,
                        public lt::session_delegate,
                        public std::enable_shared_from_this<DownloadLimiter>
{
protected:
    struct DownloadChannelInfo
    {
        uint64_t                m_prepaidDownloadSize;
        uint64_t                m_uploadedSize;
        std::vector<Key>        m_clients;
        endpoint_list           m_replicatorsList;
    };

    using ChannelMap = std::map<std::array<uint8_t,32>, DownloadChannelInfo>;

    ChannelMap m_channelMap;

    const crypto::KeyPair& m_keyPair;

    uint64_t m_receiptLimit = 32*1024; //1024*1024;

    const char* m_dbgOurPeerName = "unset";

public:
    DownloadLimiter( const crypto::KeyPair& keyPair, const char* dbgOurPeerName ) : m_keyPair(keyPair), m_dbgOurPeerName(dbgOurPeerName)
    {
    }

    bool checkDownloadLimit( const std::array<uint8_t,64>& /*signature*/,
                             const std::array<uint8_t,32>& downloadChannelId,
                            uint64_t                       downloadedSizeByClient ) override
    {
        static int maxDelay = 0;

        if ( auto it = m_channelMap.find( downloadChannelId ); it != m_channelMap.end() )
        {
            int delay = int(it->second.m_uploadedSize - downloadedSizeByClient);

            if ( maxDelay < delay )
                maxDelay = delay;

            LOG( dbgOurPeerName() << " delay: " << maxDelay );
            LOG( dbgOurPeerName() << " " << int(downloadChannelId[0]) << " %%%%%%% " << int(it->second.m_uploadedSize - downloadedSizeByClient) << " : " << it->second.m_uploadedSize << " "<< downloadedSizeByClient << "\n");
            // sendReceiptToOtherReplicators

            if ( it->second.m_uploadedSize > downloadedSizeByClient + m_receiptLimit )
                return false;
            return true;
        }
        return false;
    }

    uint8_t getUploadedSize( const std::array<uint8_t,32>& downloadChannelId ) override
    {
        if ( auto it = m_channelMap.find( downloadChannelId ); it != m_channelMap.end() )
        {
            return it->second.m_uploadedSize;
        }
        return 0;
    }

    void addChannelInfo( const std::array<uint8_t,32>&  channelId,
                         uint64_t                       prepaidDownloadSize,
                         const endpoint_list&           replicatorsList,
                         std::vector<Key>&&       clients )
    {
        //todo mutex

        if ( auto it = m_channelMap.find(channelId); it != m_channelMap.end() )
        {
            if ( it->second.m_prepaidDownloadSize <= prepaidDownloadSize )
            {
                throw std::runtime_error( "addChannelInfo: invalid prepaidDownloadSize" );
            }
            it->second.m_prepaidDownloadSize = prepaidDownloadSize;

            if ( clients.size() > 0 )
            {
                it->second.m_clients = std::move(clients);
            }
            return;
        }

        m_channelMap[channelId] = DownloadChannelInfo{ prepaidDownloadSize, 0, std::move(clients), replicatorsList };
    }

    // It will be called,
    // when a piece is sent
    virtual void onPieceSent( const std::array<uint8_t,32>& downloadChannelId, uint64_t pieceSize ) override
    {
        if ( auto it = m_channelMap.find( downloadChannelId ); it != m_channelMap.end() )
        {
            it->second.m_uploadedSize += pieceSize;
        }
        else
        {
            LOG_ERR( "ERROR: unknown downloadChannelId" );
        }
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
                      uint64_t&                     downloadedSize,
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
        return crypto::Verify( publicKey(),
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

    const std::optional<std::array<uint8_t,32>> downloadChannelId() override
    {
        return m_downloadChannelId;
    }

    uint64_t downloadedSize( const std::array<uint8_t,32>& ) override
    {
        //todo++
        return 0;
    }

    void setDownloadedSize( uint64_t /*downloadedSize*/ ) override
    {
    }

    uint64_t downloadedSize() override
    {
        return 0;
    }

    uint64_t requestedSize() override
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

    void setReceiptLimit( uint64_t newLimitInBytes ) override
    {
        m_receiptLimit = newLimitInBytes;
    }


private:
    std::optional<std::array<uint8_t,32>> m_downloadChannelId;
};

}}

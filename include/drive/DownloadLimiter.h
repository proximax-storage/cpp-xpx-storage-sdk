/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include "types.h"
#include "crypto/Signer.h"
#include <sirius_drive/session_delegate.h>
#include <map>

namespace sirius {

//
// DownloadLimiter - it manages all user files at replicator side
//
class DownloadLimiter : public lt::session_delegate, public std::enable_shared_from_this<DownloadLimiter>
{
    struct DownloadChannelInfo
    {
        size_t m_prepaidDownloadSize;
        size_t m_downloadedSize;
    };

    using ChannelMap = std::map<Hash256, DownloadChannelInfo>;

    ChannelMap m_channelMap;

    const crypto::KeyPair& m_keyPair;

    const char* m_dbgOurPeerName = "unset";

public:
    DownloadLimiter( const crypto::KeyPair& keyPair, const char* dbgOurPeerName ) : m_keyPair(keyPair), m_dbgOurPeerName(dbgOurPeerName)
    {
    }

    void addChannelInfo( Hash256 channelId, size_t prepaidDownloadSize )
    {
        if ( auto it = m_channelMap.find(channelId); it != m_channelMap.end() )
        {
            assert( it->second.m_prepaidDownloadSize > prepaidDownloadSize );
            it->second.m_prepaidDownloadSize = prepaidDownloadSize;
            return;
        }

        m_channelMap[channelId] = DownloadChannelInfo{ prepaidDownloadSize, 0 };
    }

    void removeChannelInfo( Hash256 channelId )
    {
        m_channelMap.erase( channelId );
    }

    bool checkDownloadLimit( std::vector<uint8_t>   /*reciept*/,
                             lt::sha256_hash        /*downloadChannelId*/,
                             size_t                 /*downloadedSize*/ ) override
    {

        return true;
    }

    virtual void sign( const uint8_t* bytes, size_t size, std::array<uint8_t,64>& signature ) override
    {
        crypto::Sign( m_keyPair, utils::RawBuffer{bytes,size}, reinterpret_cast<Signature&>(signature) );
    }

    virtual bool verify( const std::array<uint8_t,32>& publicKey,
                         const uint8_t* bytes, size_t size,
                         const std::array<uint8_t,64>& signature ) override
    {
        return crypto::Verify( publicKey, utils::RawBuffer{bytes,size}, signature );
    }

    virtual const std::array<uint8_t,32>& publicKey() override
    {
        return m_keyPair.publicKey().array();
    }



    virtual const char* dbgOurPeerName() override
    {
        return m_dbgOurPeerName;
    }

};

}

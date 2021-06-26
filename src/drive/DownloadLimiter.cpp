/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "types.h"
#include <sirius_drive/session_delegate.h>
#include <map>

namespace sirius {

//
// DownloadLimiter - it manages all user files at replicator side
//
class DownloadLimiter : public lt::session_delegate
{
    struct DownloadChannelInfo
    {
        size_t m_prepaidDownloadSize;
        size_t m_downloadedSize;
    };

    using ChannelMap = std::map<Hash256, DownloadChannelInfo>;

    ChannelMap m_channelMap;

public:
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

};

}

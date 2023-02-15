/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include "types.h"
#include "drive/Timer.h"
#include "crypto/Signer.h"

namespace sirius::drive {

struct RcptMessage : public std::vector<uint8_t>
{
    using Sign = std::array<uint8_t,64>;
    
    RcptMessage() = default;
    RcptMessage( const RcptMessage& ) = default;
    RcptMessage& operator=( const RcptMessage& ) = default;
    RcptMessage( RcptMessage&& ) = default;
    RcptMessage& operator=( RcptMessage&& ) = default;
    
    RcptMessage( const char* data, size_t dataSize ) : std::vector<uint8_t>( (const uint8_t*)data, ((const uint8_t*)data)+dataSize ) {}

    RcptMessage( const ChannelId&     dnChannelId,
                 const ClientKey&     clientKey,
                 const ReplicatorKey& replicatorKey,
                 uint64_t             downloadedSize, // to be downloaded size
                 const Sign&          signature )
    {
        reserve( 96+64 );
        insert( end(), dnChannelId.begin(),         dnChannelId.end() );
        insert( end(), clientKey.begin(),           clientKey.end() );
        insert( end(), replicatorKey.begin(),       replicatorKey.end() );
        insert( end(), (uint8_t*)&downloadedSize,   ((uint8_t*)&downloadedSize)+8 );
        insert( end(), signature.begin(),           signature.end() );
    }
    
    bool isValidSize() const { return size() == sizeof(ChannelId)+sizeof(ClientKey)+sizeof(ReplicatorKey)+8+sizeof(Sign); }

    const ChannelId&      channelId()      const { __ASSERT(!empty()); return *reinterpret_cast<const ChannelId*>(     &this->at(0) );   }
    const ClientKey&      clientKey()      const { __ASSERT(!empty()); return *reinterpret_cast<const ClientKey*>(     &this->at(32) );  }
    const ReplicatorKey&  replicatorKey()  const { __ASSERT(!empty()); return *reinterpret_cast<const ReplicatorKey*>( &this->at(64) );  }
    
    // to be downloaded size
    uint64_t              downloadedSize() const
    {
        if ( empty() )
        {
            return 0;
        }
        else
        {
            return *reinterpret_cast<const uint64_t*>( &this->at(96) );
        }
    }
    
    const uint64_t*       downloadedSizePtr() const { assert(!empty()); return (const uint64_t*)(    &this->at(96) );  }

    const Sign&           signature()      const { assert(!empty()); return *reinterpret_cast<const Sign*>(          &this->at(104) ); }
};

} //namespace sirius::drive

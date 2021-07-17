/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include "types.h"
#include <libtorrent/kademlia/ed25519.hpp>

namespace sirius { namespace drive {

void createKeypair(  SecretKey& /*clientSecretKey*/, PublicKey& /*clientPublicKey*/ ) {}

Signature calculateSign( const Hash256&     /*downloadChannelId*/,
                         const PublicKey&   /*clientPublicKey*/,
                         const SecretKey&   /*clientSecretKey*/,
                         uint64_t           /*downloadedBytes*/ )
{
    return Signature();
}

bool verify( const Hash256&     /*downloadChannelId*/,
             const PublicKey&   /*clientPublicKey*/,
             uint64_t           /*downloadedBytes*/,
             const Signature&   /*signature*/ )
{
    return true;
}

}}

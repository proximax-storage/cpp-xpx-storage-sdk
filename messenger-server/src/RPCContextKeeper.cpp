/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include "RPCContextKeeper.h"

namespace sirius::drive::messenger {

RPCContextKeeper::RPCContextKeeper( std::shared_ptr<RPCContext>&& context,
                                    uint64_t id,
                                    ConnectionManager& connectionManager )
        : m_context(std::move(context))
        , m_id( id )
        , m_connectionManager( connectionManager ) {

}

void
RPCContextKeeper::onConnectionBrokenDetected() {
    m_connectionManager.onConnectionBroken( m_id );
}

RPCContext& RPCContextKeeper::context() {
    return *m_context;
}

}
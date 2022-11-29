/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "StreamContextKeeper.h"

namespace sirius::drive::messenger
{

StreamContextKeeper::StreamContextKeeper( std::shared_ptr<StreamContext>&& context,
                                    uint64_t id,
                                    ConnectionManager& connectionManager )
        : m_context( std::move( context ))
        , m_id( id )
        , m_connectionManager( connectionManager )
{

}

StreamContextKeeper::~StreamContextKeeper()
{
    if ( m_context )
    {
        m_context->finish();
    }
}

void
StreamContextKeeper::onConnectionBrokenDetected()
{
    m_connectionManager.onConnectionBroken( m_id );
}

StreamContext& StreamContextKeeper::context()
{
    return *m_context;
}

}
/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include <messenger-server/Message.h>

#include "ReadRPCTag.h"

namespace sirius::drive::messenger
{

ReadRPCTag::ReadRPCTag( std::weak_ptr<IOContextProvider> context, std::shared_ptr<ReadEventHandler> handler )
        : m_context( std::move( context )), m_eventHandler( std::move( handler ))
{}

void ReadRPCTag::process( bool ok )
{

    auto contextKeeper = m_context.lock();

    if ( !contextKeeper )
    {
        return;
    }

    if ( ok )
    {
        boost::asio::post( contextKeeper->getContext(),
                           [message = std::move( m_message ), handler = std::move( m_eventHandler )]
                           {
                               handler->onRead( message );
                           } );
    } else
    {
        boost::asio::post( contextKeeper->getContext(), [handler = std::move( m_eventHandler )]
        {
            handler->onRead( {} );
        } );
    }
}

}
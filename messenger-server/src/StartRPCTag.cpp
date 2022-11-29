/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "StartRPCTag.h"

#include <boost/asio.hpp>

namespace sirius::drive::messenger
{

StartRPCTag::StartRPCTag( std::weak_ptr<ConnectionManager> connectionManager, std::shared_ptr<StreamContext> context )
        : m_connectionManager( std::move( connectionManager ))
        , m_context( std::move( context ))
{}

void StartRPCTag::process( bool ok )
{
    if ( !ok )
    {
        return;
    }

    auto contextKeeper = m_context->m_ioContext.lock();

    if ( !contextKeeper )
    {
        return;
    }

    boost::asio::post( contextKeeper->getContext(),
                       [connectionManager = std::move( m_connectionManager ),
                        context = std::move( m_context )]() mutable
                       {
                           auto manager = connectionManager.lock();

                           if ( manager )
                           {
                               manager->onConnectionEstablished( std::move( context ));
                           }
                       } );
}

}
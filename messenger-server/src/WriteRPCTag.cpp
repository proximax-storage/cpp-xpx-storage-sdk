/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "WriteRPCTag.h"

#include <boost/asio.hpp>

sirius::drive::messenger::WriteRPCTag::WriteRPCTag( std::weak_ptr<IOContextProvider> context,
                                                    std::shared_ptr<WriteEventHandler> handler )
        : m_context( std::move( context ))
        , m_handler( std::move( handler ))
{

}

void sirius::drive::messenger::WriteRPCTag::process( bool ok )
{

    auto contextKeeper = m_context.lock();

    if ( !contextKeeper )
    {
        return;
    }

    boost::asio::post( contextKeeper->getContext(), [=, handler = std::move( m_handler )]
    {
        handler->onWritten( ok );
    } );

}

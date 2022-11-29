/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "AcceptRequestRPCTag.h"
#include <boost/asio.hpp>
#include <utility>

namespace sirius::drive::contract
{

void AcceptRequestRPCTag::process( bool ok )
{
    auto c = m_ioContext.lock();

    if ( !c )
    {
        return;
    }

    if ( ok )
    {
        boost::asio::post( c->getContext(), [requestContext = m_requestContext,
                addNewAcceptRequestTag = std::move( m_addNewAcceptRequestTag )]
        {
            requestContext->processRequest();
            addNewAcceptRequestTag();
        } );
    }
}

AcceptRequestRPCTag::AcceptRequestRPCTag( std::shared_ptr<RequestContext> requestContext,
                                          std::function<void()> addNewAcceptRequestTag,
                                          std::weak_ptr<IOContextProvider> ioContext )
        : m_requestContext( std::move( requestContext ))
        , m_addNewAcceptRequestTag( std::move( addNewAcceptRequestTag ))
        , m_ioContext( std::move( ioContext ))
{}

}
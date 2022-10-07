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
    if ( ok )
    {
        boost::asio::post( m_ioContext, [requestContext = m_requestContext]
        {
            requestContext->processRequest();
        } );
    }
}

AcceptRequestRPCTag::AcceptRequestRPCTag( std::shared_ptr<RequestContext> requestContext,
                                          std::function<void()> addNewAcceptRequestTag,
                                          boost::asio::io_context& ioContext )
        : m_requestContext( std::move( requestContext ))
        , m_addNewAcceptRequestTag(std::move( addNewAcceptRequestTag ))
        , m_ioContext( ioContext )
{}

}
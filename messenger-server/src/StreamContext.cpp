/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "StreamContext.h"
#include "FinishRPCTag.h"

namespace sirius::drive::messenger
{

StreamContext::StreamContext( std::weak_ptr<IOContextProvider> io_context )
        : m_ioContext( std::move( io_context ))
        , m_stream( &m_serverContext )
{

}

void StreamContext::finish()
{
    m_serverContext.TryCancel();
    auto* tag = new FinishRPCTag( shared_from_this());
    m_stream.Finish( grpc::Status::CANCELLED, tag );
}

}
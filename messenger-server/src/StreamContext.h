/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include <boost/asio/io_context.hpp>
#include "messengerServer.grpc.pb.h"
#include <grpcpp/impl/codegen/async_stream.h>
#include <grpcpp/impl/codegen/server_context.h>
#include <drive/IOContextProvider.h>

#include <memory>

namespace sirius::drive::messenger
{

class StreamContext
        : public std::enable_shared_from_this<StreamContext>
{

public:

    StreamContext( std::weak_ptr<IOContextProvider> io_context );

    grpc::ServerContext m_serverContext;
    std::weak_ptr<IOContextProvider> m_ioContext;
    grpc::ServerAsyncReaderWriter<messengerServer::ServerMessage, messengerServer::ClientMessage> m_stream;

    void finish();

};

}
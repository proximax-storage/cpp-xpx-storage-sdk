/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once


#include <grpcpp/impl/codegen/server_context.h>

#include <boost/asio/io_context.hpp>
#include <grpcpp/impl/codegen/async_stream.h>

#include "messengerServer.pb.h"

namespace sirius::drive::messenger
{

class RPCContext {

public:

    grpc::ServerContext m_serverContext;
    std::shared_ptr<bool> m_serviceIsActive;
    boost::asio::io_context& m_ioContext;
    grpc::ServerAsyncReaderWriter<messengerServer::ServerMessage, messengerServer::ClientMessage> m_stream;

};

}
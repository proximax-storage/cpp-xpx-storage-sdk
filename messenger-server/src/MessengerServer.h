/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include "RPCContextKeeper.h"
#include "ConnectionManager.h"

#include "messengerServer.grpc.pb.h"

#include <thread>

namespace sirius::drive::messenger
{

class MessengerServer: public ConnectionManager, std::enable_shared_from_this<MessengerServer>
{

private:

    boost::asio::io_context& m_executor;

    messengerServer::MessengerServer::AsyncService m_service;
    std::unique_ptr<grpc::ServerCompletionQueue> m_completionQueue;
    std::thread m_completionQueueThread;

    uint64_t m_connectionsCreated = 0;
    std::map<uint64_t, std::shared_ptr<RPCContextKeeper>> m_connections;

public:

    void onConnectionBroken( uint64_t id ) override;

    void onConnectionEstablished( std::shared_ptr<RPCContext> context ) override;

private:

    void run(grpc::ServerBuilder& builder);

    void accept();

    void waitForQueries();
};

}
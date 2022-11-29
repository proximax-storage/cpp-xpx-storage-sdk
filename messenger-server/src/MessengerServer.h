/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include "StreamContextKeeper.h"
#include "ConnectionManager.h"
#include "drive/IOContextProvider.h"
#include <messenger-server/Messenger.h>
#include "messengerServer.grpc.pb.h"
#include <drive/RPCService.h>

#include <thread>

namespace sirius::drive::messenger
{

class MessengerServer
        : public ConnectionManager, public RPCService, public std::enable_shared_from_this<MessengerServer>
{

private:

    std::weak_ptr<IOContextProvider> m_executor;
    std::weak_ptr<Messenger> m_messenger;

    messengerServer::MessengerServer::AsyncService m_service;
    std::unique_ptr<grpc::ServerCompletionQueue> m_completionQueue;
    std::thread m_completionQueueThread;

    uint64_t m_connectionsCreated = 0;
    std::map<uint64_t, std::shared_ptr<StreamContextKeeper>> m_connections;

public:

    explicit MessengerServer( std::weak_ptr<Messenger>&& messenger );

    ~MessengerServer() override;

    void onConnectionBroken( uint64_t id ) override;

    void onConnectionEstablished( std::shared_ptr<StreamContext> context ) override;

public:
    void registerService( grpc::ServerBuilder& builder ) override;

    void run( std::weak_ptr<IOContextProvider> contextKeeper ) override;

    void accept();

    void waitForQueries();
};

}
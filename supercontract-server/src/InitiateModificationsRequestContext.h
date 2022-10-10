/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include <memory>
#include <grpcpp/impl/codegen/server_context.h>
#include <grpcpp/impl/codegen/async_unary_call.h>
#include <drive/ManualModificationsRequests.h>
#include "storageServer.pb.h"
#include <supercontract-server/StorageServer.h>
#include "AcceptRequestRPCTag.h"
#include "RequestContext.h"

namespace sirius::drive::contract
{

class InitiateModificationsRequestContext
        : public RequestContext, public std::enable_shared_from_this<InitiateModificationsRequestContext>
{

private:

    bool m_responseAlreadyGiven = false;

    storage::StorageServer::AsyncService& m_service;
    grpc::ServerCompletionQueue& m_completionQueue;

    std::shared_ptr<bool> m_serviceIsActive;

    grpc::ServerContext m_context;

    storage::InitModificationsRequest m_request;
    grpc::ServerAsyncResponseWriter<storage::InitModificationsResponse> m_responder;

    std::weak_ptr<ModificationsExecutor> m_executor;

public:

    InitiateModificationsRequestContext( storage::StorageServer::AsyncService& service,
                                      grpc::ServerCompletionQueue& completionQueue,
                                      std::shared_ptr<bool> serviceIsActive,
                                      std::weak_ptr<ModificationsExecutor> executor );

    void run( AcceptRequestRPCTag* tag )
    {
        m_service.RequestInitiateModifications( &m_context, &m_request, &m_responder, &m_completionQueue,
                                             &m_completionQueue, tag );
    }

    void processRequest() override;

private:

    void onCallExecuted( const std::optional<InitiateModificationsResponse>& response );
};

}
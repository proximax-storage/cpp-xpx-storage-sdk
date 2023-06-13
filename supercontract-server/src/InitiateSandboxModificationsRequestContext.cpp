/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "InitiateSandboxModificationsRequestContext.h"

#include <utility>
#include "drive/ManualModificationsRequests.h"
#include "FinishRequestRPCTag.h"

namespace sirius::drive::contract
{

InitiateSandboxModificationsRequestContext::InitiateSandboxModificationsRequestContext(
        storageServer::StorageServer::AsyncService& service,
        grpc::ServerCompletionQueue& completionQueue,
        std::shared_ptr<bool> serviceIsActive,
        std::weak_ptr<ModificationsExecutor> executor )
        : m_service( service )
        , m_completionQueue( completionQueue )
        , m_serviceIsActive( std::move( serviceIsActive ))
        , m_responder( &m_context )
        , m_executor( std::move( executor ))
{}

void InitiateSandboxModificationsRequestContext::processRequest()
{
    if ( !*m_serviceIsActive )
    {
        return;
    }

    auto executor = m_executor.lock();

    if ( !executor )
    {
        onCallExecuted( {} );
        return;
    }

    Key driveKey( *reinterpret_cast<const std::array<uint8_t, 32>*>(m_request.drive_key().data()));
    std::vector<std::string> serviceFolders;
    for (int i = 0; i < m_request.service_folders_size(); i++) {
        serviceFolders.emplace_back(m_request.service_folders(i).begin(),
                                  m_request.service_folders(i).end());
    }
    InitiateSandboxModificationsRequest request( serviceFolders, [pThis = shared_from_this()]( auto response ) {
        pThis->onCallExecuted( response );
    } );
    executor->initiateManualSandboxModifications( driveKey, request );
}

void InitiateSandboxModificationsRequestContext::onCallExecuted(
        const std::optional<InitiateSandboxModificationsResponse>& response )
{
    if ( !*m_serviceIsActive )
    {
        return;
    }

    if ( m_responseAlreadyGiven )
    {
        return;
    }

    m_responseAlreadyGiven = true;

    storageServer::InitSandboxResponse msg;
    grpc::Status status;
    if ( response )
    {
    } else
    {
        status = grpc::Status::CANCELLED;
    }
    auto* tag = new FinishRequestRPCTag( shared_from_this());
    m_responder.Finish( msg, status, tag );
}

}
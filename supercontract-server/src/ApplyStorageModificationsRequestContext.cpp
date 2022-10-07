/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "ApplyStorageModificationsRequestContext.h"

#include <utility>
#include "drive/ManualModificationsRequests.h"

namespace sirius::drive::contract
{

ApplyStorageModificationsRequestContext::ApplyStorageModificationsRequestContext(
        storage::StorageServer::AsyncService& service,
        grpc::ServerCompletionQueue& completionQueue,
        std::shared_ptr<bool> serviceIsActive,
        std::weak_ptr<ModificationsExecutor> executor )
        : m_service( service )
        , m_completionQueue( completionQueue )
        , m_serviceIsActive( std::move( serviceIsActive ))
        , m_responder( &m_context )
        , m_executor( std::move( executor ))
{}

void ApplyStorageModificationsRequestContext::processRequest()
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

    ApplyStorageModificationsRequest request;
    Key driveKey( *reinterpret_cast<const std::array<uint8_t, 32>*>(m_request.drive_key().data()));
    request.m_success = m_request.success();
    request.m_callback = [pThis = shared_from_this()]( auto response )
    {
        pThis->onCallExecuted( response );
    };
    executor->applyStorageManualModifications( driveKey, request );
}

void ApplyStorageModificationsRequestContext::onCallExecuted( const std::optional<ApplyStorageModificationsResponse>& response )
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

    storage::ApplyStorageModificationsResponse msg;
    grpc::Status status;
    if ( response )
    {
    } else
    {
        status = grpc::Status::CANCELLED;
    }
    m_responder.Finish( msg, status, nullptr );
}

}
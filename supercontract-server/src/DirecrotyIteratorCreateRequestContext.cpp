/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "DirectoryIteratorCreateRequestContext.h"

#include <utility>
#include "drive/ManualModificationsRequests.h"
#include "FinishRequestRPCTag.h"

namespace sirius::drive::contract
{

DirectoryIteratorCreateRequestContext::DirectoryIteratorCreateRequestContext(
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

void DirectoryIteratorCreateRequestContext::processRequest()
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

    FolderIteratorCreateRequest request;
    Key driveKey( *reinterpret_cast<const std::array<uint8_t, 32>*>(m_request.drive_key().data()));
    request.m_path = m_request.path();
    request.m_recursive = m_request.recursive();
    request.m_callback = [pThis = shared_from_this()]( auto response )
    {
        pThis->onCallExecuted( response );
    };

    executor->folderIteratorCreate( driveKey, request );
}

void DirectoryIteratorCreateRequestContext::onCallExecuted( const std::optional<FolderIteratorCreateResponse>& response )
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

    storageServer::DirectoryIteratorCreateResponse msg;
    grpc::Status status;
    if ( response )
    {
        msg.set_success( response->m_id.has_value());
        if ( response->m_id.has_value())
        {
            msg.set_id( *response->m_id );
        }
    } else
    {
        status = grpc::Status::CANCELLED;
    }
    auto* tag = new FinishRequestRPCTag( shared_from_this());
    m_responder.Finish( msg, status, tag );
}

}
/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "CloseFileRequestContext.h"

#include <utility>
#include "drive/ManualModificationsRequests.h"
#include "FinishRequestRPCTag.h"

namespace sirius::drive::contract
{

CloseFileRequestContext::CloseFileRequestContext(
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

void CloseFileRequestContext::processRequest()
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

    CloseFileRequest request;
    Key driveKey( *reinterpret_cast<const std::array<uint8_t, 32>*>(m_request.drive_key().data()));
    request.m_fileId = m_request.file_id();
    request.m_callback = [pThis = shared_from_this()]( auto response )
    {
        pThis->onCallExecuted( response );
    };

    executor->closeFile( driveKey, request );
}

void CloseFileRequestContext::onCallExecuted( const std::optional<CloseFileResponse>& response )
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

    storageServer::CloseFileResponse msg;
    grpc::Status status;
    if ( response )
    {
        msg.set_success( response->m_success );
    } else
    {
        status = grpc::Status::CANCELLED;
    }
    auto* tag = new FinishRequestRPCTag( shared_from_this());
    m_responder.Finish( msg, status, tag );
}

}
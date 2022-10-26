/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "DirectoryIteratorNextRequestContext.h"

#include <utility>
#include "drive/ManualModificationsRequests.h"
#include "FinishRequestRPCTag.h"

namespace sirius::drive::contract
{

DirectoryIteratorNextRequestContext::DirectoryIteratorNextRequestContext(
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

void DirectoryIteratorNextRequestContext::processRequest()
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

    FolderIteratorNextRequest request;
    Key driveKey( *reinterpret_cast<const std::array<uint8_t, 32>*>(m_request.drive_key().data()));
    request.m_id = m_request.id();
    request.m_callback = [pThis = shared_from_this()]( auto response )
    {
        pThis->onCallExecuted( response );
    };

    executor->folderIteratorNext( driveKey, request );
}

void DirectoryIteratorNextRequestContext::onCallExecuted( const std::optional<FolderIteratorNextResponse>& response )
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

    storageServer::DirectoryIteratorNextResponse msg;
    grpc::Status status;
    if ( response )
    {
        if ( response->m_name )
        {
            msg.set_success( true );
            msg.set_name( *response->m_name );
        } else
        {
            msg.set_success( false );
        }
    } else
    {
        status = grpc::Status::CANCELLED;
    }
    auto* tag = new FinishRequestRPCTag( shared_from_this());
    m_responder.Finish( msg, status, tag );
}

}
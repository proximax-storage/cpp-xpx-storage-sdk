/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "EvaluateStorageHashRequestContext.h"

#include <utility>
#include "drive/ManualModificationsRequests.h"

namespace sirius::drive::contract
{

EvaluateStorageHashRequestContext::EvaluateStorageHashRequestContext(
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

void EvaluateStorageHashRequestContext::processRequest()
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

    EvaluateStorageHashRequest request;
    Key driveKey( *reinterpret_cast<const std::array<uint8_t, 32>*>(m_request.drive_key().data()));
    request.m_callback = [pThis = shared_from_this()]( auto response )
    {
        pThis->onCallExecuted( response );
    };
    executor->evaluateStorageHash( driveKey, request );
}

void EvaluateStorageHashRequestContext::onCallExecuted( const std::optional<EvaluateStorageHashResponse>& response )
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

    storage::EvaluateStorageHashResponse msg;
    grpc::Status status;
    if ( response )
    {
        msg.set_storage_hash(std::string(response->m_state.begin(), response->m_state.end()));
        msg.set_used_drive_size(response->m_usedDriveSize);
        msg.set_meta_files_size(response->m_metaFilesSize);
        msg.set_file_structure_size(response->m_fileStructureSize);
    } else
    {
        status = grpc::Status::CANCELLED;
    }
    m_responder.Finish( msg, status, nullptr );
}

}
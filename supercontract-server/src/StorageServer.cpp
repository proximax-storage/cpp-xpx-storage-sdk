/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server_builder.h>

#include <utility>
#include "StorageServer.h"
#include "RPCTag.h"
#include "SynchronizeStorageRequestContext.h"
#include "InitiateModificationsRequestContext.h"
#include "InitiateSandboxModificationsRequestContext.h"
#include "ApplySandboxModificationsRequestContext.h"
#include "EvaluateStorageHashRequestContext.h"
#include "ApplyStorageModificationsRequestContext.h"
#include "OpenFileRequestContext.h"
#include "ReadFileRequestContext.h"
#include "WriteFileRequestContext.h"
#include "CloseFileRequestContext.h"
#include "FlushRequestContext.h"

namespace sirius::drive::contract
{

StorageServer::StorageServer( std::string address,
                              boost::asio::io_context& context )
        : m_address( std::move( address ))
        , m_context( context )
{}

void StorageServer::run( std::weak_ptr<ModificationsExecutor> executor )
{
    m_executor = std::move( executor );
    grpc::ServerBuilder builder;
    builder.AddListeningPort( m_address, grpc::InsecureServerCredentials());
    builder.RegisterService( &m_service );
    m_cq = builder.AddCompletionQueue();
    m_server = builder.BuildAndStart();
    m_serviceIsActive = std::make_shared<bool>( true );

    registerSynchronizeStorage();
    registerInitiateModifications();
    registerInitiateSandboxModifications();
    registerApplySandboxStorageModifications();
    registerEvaluateStorageHash();
    registerApplyStorageModifications();
    registerOpenFile();
    registerReadFile();
    registerWriteFile();
    registerCloseFile();
    registerFlush();

    m_thread = std::thread( [this]
                            {
                                waitForQueries();
                            } );
}

StorageServer::~StorageServer()
{
    if ( !*m_serviceIsActive )
    {
        return;
    }
    *m_serviceIsActive = false;
    m_server->Shutdown();
    m_cq->Shutdown();
    if ( m_thread.joinable())
    {
        m_thread.join();
    }
}

void StorageServer::waitForQueries()
{
    void* pTag;
    bool ok;
    while ( m_cq->Next( &pTag, &ok ))
    {
        if ( pTag != nullptr )
        {
            auto* pQuery = static_cast<RPCTag*>(pTag);
            pQuery->process( ok );
            delete pQuery;
        }
    }
}

void StorageServer::registerSynchronizeStorage()
{
    if ( !*m_serviceIsActive )
    {
        return;
    }

    auto context = std::make_shared<SynchronizeStorageRequestContext>( m_service, *m_cq, m_serviceIsActive, m_executor );
    auto* tag = new AcceptRequestRPCTag( context, [this]
    {
        registerSynchronizeStorage();
    }, m_context );
    context->run( tag );
}

void StorageServer::registerInitiateModifications()
{
    if ( !*m_serviceIsActive )
    {
        return;
    }

    auto synchronizeContext = std::make_shared<InitiateModificationsRequestContext>( m_service, *m_cq, m_serviceIsActive, m_executor );
    auto* tag = new AcceptRequestRPCTag( synchronizeContext, [this]
    {
        registerInitiateModifications();
        }, m_context );
    synchronizeContext->run( tag );
}

void StorageServer::registerInitiateSandboxModifications()
{
    if ( !*m_serviceIsActive )
    {
        return;
    }

    auto synchronizeContext = std::make_shared<InitiateSandboxModificationsRequestContext>( m_service, *m_cq, m_serviceIsActive, m_executor );
    auto* tag = new AcceptRequestRPCTag( synchronizeContext, [this]
    {
        registerInitiateSandboxModifications();
        }, m_context );
    synchronizeContext->run( tag );
}

void StorageServer::registerApplySandboxStorageModifications()
{
    if ( !*m_serviceIsActive )
    {
        return;
    }

    auto synchronizeContext = std::make_shared<ApplySandboxModificationsRequestContext>( m_service, *m_cq, m_serviceIsActive, m_executor );
    auto* tag = new AcceptRequestRPCTag( synchronizeContext, [this]
    {
        registerApplySandboxStorageModifications();
        }, m_context );
    synchronizeContext->run( tag );
}

void StorageServer::registerEvaluateStorageHash()
{
    if ( !*m_serviceIsActive )
    {
        return;
    }

    auto synchronizeContext = std::make_shared<EvaluateStorageHashRequestContext>( m_service, *m_cq, m_serviceIsActive, m_executor );
    auto* tag = new AcceptRequestRPCTag( synchronizeContext, [this]
    {
        registerEvaluateStorageHash();
        }, m_context );
    synchronizeContext->run( tag );
}

void StorageServer::registerApplyStorageModifications()
{
    if ( !*m_serviceIsActive )
    {
        return;
    }

    auto synchronizeContext = std::make_shared<ApplyStorageModificationsRequestContext>( m_service, *m_cq, m_serviceIsActive, m_executor );
    auto* tag = new AcceptRequestRPCTag( synchronizeContext, [this]
    {
        registerApplyStorageModifications();
        }, m_context );
    synchronizeContext->run( tag );
}

void StorageServer::registerOpenFile()
{
    if ( !*m_serviceIsActive )
    {
        return;
    }

    auto synchronizeContext = std::make_shared<OpenFileRequestContext>( m_service, *m_cq, m_serviceIsActive, m_executor );
    auto* tag = new AcceptRequestRPCTag( synchronizeContext, [this]
    {
        registerOpenFile();
        }, m_context );
    synchronizeContext->run( tag );
}

void StorageServer::registerReadFile()
{
    if ( !*m_serviceIsActive )
    {
        return;
    }

    auto synchronizeContext = std::make_shared<ReadFileRequestContext>( m_service, *m_cq, m_serviceIsActive, m_executor );
    auto* tag = new AcceptRequestRPCTag( synchronizeContext, [this]
    {
        registerReadFile();
        }, m_context );
    synchronizeContext->run( tag );
}

void StorageServer::registerWriteFile()
{
    if ( !*m_serviceIsActive )
    {
        return;
    }

    auto synchronizeContext = std::make_shared<WriteFileRequestContext>( m_service, *m_cq, m_serviceIsActive, m_executor );
    auto* tag = new AcceptRequestRPCTag( synchronizeContext, [this]
    {
        registerWriteFile();
        }, m_context );
    synchronizeContext->run( tag );
}

void StorageServer::registerCloseFile()
{
    if ( !*m_serviceIsActive )
    {
        return;
    }

    auto synchronizeContext = std::make_shared<CloseFileRequestContext>( m_service, *m_cq, m_serviceIsActive, m_executor );
    auto* tag = new AcceptRequestRPCTag( synchronizeContext, [this]
    {
        registerCloseFile();
        }, m_context );
    synchronizeContext->run( tag );
}

void StorageServer::registerFlush()
{
    if ( !*m_serviceIsActive )
    {
        return;
    }

    auto synchronizeContext = std::make_shared<FlushRequestContext>( m_service, *m_cq, m_serviceIsActive, m_executor );
    auto* tag = new AcceptRequestRPCTag( synchronizeContext, [this]
    {
        registerFlush();
        }, m_context );
    synchronizeContext->run( tag );
}

}

/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "MessengerServer.h"

#include "StartRPCTag.h"
#include "MessageReader.h"

#include <grpcpp/server_builder.h>

namespace sirius::drive::messenger
{

MessengerServer::MessengerServer( std::weak_ptr<Messenger>&& messenger )
        : m_messenger( std::move( messenger ))
{}

MessengerServer::~MessengerServer()
{
	for (const auto& [id, context]: m_connections) {
		context->stop();
	}
    m_completionQueue->Shutdown();
    if ( m_completionQueueThread.joinable())
    {
        m_completionQueueThread.join();
    }
}

void MessengerServer::registerService( grpc::ServerBuilder& builder )
{
    builder.RegisterService( &m_service );
    m_completionQueue = builder.AddCompletionQueue();
}

void MessengerServer::run( std::weak_ptr<IOContextProvider> contextKeeper )
{
    m_executor = std::move( contextKeeper );

    m_completionQueueThread = std::thread( [this]
                                           {
                                               waitForQueries();
                                           } );

    accept();
}

void MessengerServer::onConnectionBroken( uint64_t id )
{
    m_connections.erase( id );
}

void MessengerServer::onConnectionEstablished( std::shared_ptr<StreamContext> context )
{
    auto connectionId = m_connectionsCreated++;
    auto contextKeeper = std::make_shared<StreamContextKeeper>( std::move( context ), connectionId, *this );
    m_connections.emplace( connectionId, contextKeeper );
    auto messageReader = std::make_shared<MessageReader>( m_messenger, contextKeeper );
    messageReader->read();
    accept();
}

void MessengerServer::accept()
{
    auto context = std::make_shared<StreamContext>( m_executor );

    auto* tag = new StartRPCTag( weak_from_this(), context );

    m_service.RequestCommunicate( &context->m_serverContext, &context->m_stream, m_completionQueue.get(),
                                  m_completionQueue.get(), tag );
}

void MessengerServer::waitForQueries()
{
    void* pTag;
    bool ok;
    while ( m_completionQueue->Next( &pTag, &ok ))
    {
        auto* pQuery = static_cast<RPCTag*>(pTag);
        pQuery->process( ok );
        delete pQuery;
    }
}

}
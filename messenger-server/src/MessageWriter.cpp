/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "MessageWriter.h"

#include "WriteRPCTag.h"

#include "messengerServer.grpc.pb.h"

sirius::drive::messenger::MessageWriter::MessageWriter( std::weak_ptr<StreamContextKeeper> context )
        : m_context( std::move( context ))
{}

bool sirius::drive::messenger::MessageWriter::onMessageReceived( const InputMessage& message )
{

    if (!m_context.lock()) {
        return false;
    }

    m_messages.push( message );

    if ( !m_writeIsRunning )
    {
        write();
    }

    return true;
}

void sirius::drive::messenger::MessageWriter::write()
{

    auto contextKeeper = m_context.lock();

    if ( !contextKeeper || contextKeeper->isStopped() )
    {
    	return;
    }

    auto& context = contextKeeper->context();

    auto message = std::move( m_messages.front());
    m_messages.pop();

    auto* inputMessage = new messengerServer::InputMessage();
    inputMessage->set_tag( message.m_tag );
    inputMessage->set_content( message.m_data );

    messengerServer::ServerMessage serverMessage;
    serverMessage.set_allocated_input_message( inputMessage );

    auto* tag = new WriteRPCTag( context.m_ioContext, shared_from_this());

    m_writeIsRunning = true;
    context.m_stream.Write( serverMessage, tag );
}

void sirius::drive::messenger::MessageWriter::onWritten( bool ok )
{
    m_writeIsRunning = false;

    if ( ok )
    {
        if ( !m_messages.empty())
        {
            write();
        }
    } else
    {
        auto contextKeeper = m_context.lock();

		if ( !contextKeeper || contextKeeper->isStopped() )
		{
			return;
		}

		contextKeeper->onConnectionBrokenDetected();
    }
}

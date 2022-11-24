/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "MessageWriter.h"

#include "WriteRPCTag.h"

#include "messengerServer.grpc.pb.h"

sirius::drive::messenger::MessageWriter::MessageWriter( std::shared_ptr<RPCContext> context )
        : m_context(std::move( context ))
        {}

void sirius::drive::messenger::MessageWriter::onMessageReceived( const std::string& tag, const std::string& message ) {

    if ( !*m_context->m_serviceIsActive ) {
        return;
    }

    m_messages.push( InputMessage{tag, message} );

    if ( !m_writeIsRunning ) {
        write();
    }

}

void sirius::drive::messenger::MessageWriter::write() {

    if ( !*m_context->m_serviceIsActive ) {
        return;
    }

    auto message = std::move( m_messages.front());
    m_messages.pop();

    auto* inputMessage = new messengerServer::InputMessage();
    inputMessage->set_tag( message.m_tag );
    inputMessage->set_content( message.m_data );

    messengerServer::ServerMessage serverMessage;
    serverMessage.set_allocated_input_message( inputMessage );

    auto* tag = new WriteRPCTag( m_context->m_ioContext, shared_from_this());

    m_writeIsRunning = true;
    m_context->m_stream.Write( serverMessage, tag );
}

void sirius::drive::messenger::MessageWriter::onWritten( bool ok ) {

    m_writeIsRunning = false;

    if ( ok && m_context->m_serviceIsActive ) {
        if ( !m_messages.empty()) {
            write();
        }
    }

}

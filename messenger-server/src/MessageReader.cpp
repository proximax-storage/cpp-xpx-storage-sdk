/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include <optional>
#include "MessageReader.h"
#include "ReadRPCTag.h"

namespace sirius::drive::messenger
{

MessageReader::MessageReader( std::weak_ptr<Messenger> messenger, std::weak_ptr<StreamContextKeeper> context )
        : m_messenger( std::move( messenger )), m_context( std::move( context ))
{}

void MessageReader::read()
{
    auto contextKeeper = m_context.lock();

    if ( !contextKeeper || contextKeeper->isStopped() )
    {
    	return;
    }

    auto* tag = new ReadRPCTag( contextKeeper->context().m_ioContext, shared_from_this());
    contextKeeper->context().m_stream.Read( &tag->m_message, tag );
}

void MessageReader::onRead( const std::optional<messengerServer::ClientMessage>& message )
{
    if ( !message )
    {
        auto contextKeeper = m_context.lock();
        if ( !contextKeeper || contextKeeper->isStopped() )
        {
        	return;
        }

		contextKeeper->onConnectionBrokenDetected();
        return;
    }

    auto messenger = m_messenger.lock();

    if ( !messenger )
    {
        return;
    }

    if ( message->has_subscribe())
    {
        // Lazy initialization of m_writer
        if ( !m_writer )
        {
            m_writer = std::make_shared<MessageWriter>( m_context );
        }
        messenger->subscribe( message->subscribe().tag(), m_writer );
    } else if ( message->has_output_message())
    {
        const auto& grpcOutputMessage = message->output_message();
        OutputMessage outputMessage;
        outputMessage.m_receiver = *reinterpret_cast<const Key*>(grpcOutputMessage.receiver().data());
        outputMessage.m_tag = grpcOutputMessage.tag();
        outputMessage.m_data = grpcOutputMessage.content();
        messenger->sendMessage( outputMessage );
    }

    read();
}

}
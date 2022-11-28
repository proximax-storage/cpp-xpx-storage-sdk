/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "MessageReader.h"

#include "ReadRPCTag.h"

namespace sirius::drive::messenger {

MessageReader::MessageReader( Messenger& messenger, std::weak_ptr<RPCContextKeeper> context )
        : m_messenger( messenger ), m_context( std::move( context )) {
    read();
}

void MessageReader::read() {
    auto contextKeeper = m_context.lock();

    if (!contextKeeper) {
        return;
    }

    auto* tag = new ReadRPCTag(contextKeeper->context().m_ioContext, shared_from_this());
    contextKeeper->context().m_stream.Read(&tag->m_message, tag);
}

void MessageReader::onRead( const std::optional<messengerServer::ClientMessage>& message ) {
    if ( !message ) {
        auto contextKeeper = m_context.lock();
        if ( contextKeeper ) {
            contextKeeper->onConnectionBrokenDetected();
        }
        return;
    }

    if ( message->has_subscribe()) {
        if ( !m_writer ) {
            m_writer = std::make_shared<MessageWriter>( m_context );
        }
        m_messenger.subscribe( message->subscribe().tag(), m_writer );
    } else if ( message->has_output_message()) {
        const auto& grpcOutputMessage = message->output_message();
        OutputMessage outputMessage;
        outputMessage.m_receiver = *reinterpret_cast<const Key*>(grpcOutputMessage.receiver().data());
        outputMessage.m_tag = grpcOutputMessage.tag();
        outputMessage.m_data = grpcOutputMessage.content();
        m_messenger.sendMessage( outputMessage );
    }

    read();
}

}
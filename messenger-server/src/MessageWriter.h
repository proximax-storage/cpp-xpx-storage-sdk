/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include <memory>
#include <queue>

#include <messenger-server/Message.h>
#include <messenger-server/MessageSubscriber.h>
#include "StreamContextKeeper.h"
#include "WriteEventHandler.h"

namespace sirius::drive::messenger
{

class MessageWriter
        : public std::enable_shared_from_this<MessageWriter>, public MessageSubscriber, public WriteEventHandler
{

    std::queue<InputMessage> m_messages;
    std::weak_ptr<StreamContextKeeper> m_context;
    bool m_writeIsRunning = false;

public:

    MessageWriter( std::weak_ptr<StreamContextKeeper> context );

    bool onMessageReceived( const InputMessage& message ) override;

    void onWritten( bool ok ) override;

private:

    void write();

};

}
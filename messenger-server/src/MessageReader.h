/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include <memory>
#include "StreamContextKeeper.h"
#include <messengerServer.pb.h>
#include "ReadEventHandler.h"

#include <messenger-server/Messenger.h>

#include "MessageWriter.h"

namespace sirius::drive::messenger
{

class MessageReader
        : public std::enable_shared_from_this<MessageReader>, public ReadEventHandler
{

    std::weak_ptr<Messenger> m_messenger;
    std::weak_ptr<StreamContextKeeper> m_context;
    std::shared_ptr<MessageWriter> m_writer;

public:

    MessageReader( std::weak_ptr<Messenger> messenger, std::weak_ptr<StreamContextKeeper> );

    void read();

    void onRead( const std::optional<messengerServer::ClientMessage>& message ) override;

};

}
/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include <memory>
#include "RPCContext.h"
#include <messenger-server/MessageSender.h>
#include "ReadEventHandler.h"

namespace sirius::drive::messenger {

class MessageReader
        : public std::enable_shared_from_this<MessageReader>, public ReadEventHandler {

    MessageSender& m_sender;
    std::shared_ptr<RPCContext> m_context;

public:

    void onRead( const std::optional<OutputMessage>& message ) override;

private:

    void read();

};

}
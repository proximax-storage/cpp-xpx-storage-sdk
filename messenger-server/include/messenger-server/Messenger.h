/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include "Message.h"

#include "MessageSubscriber.h"

namespace sirius::drive::messenger
{

class Messenger {

public:

    virtual ~Messenger() = default;

    virtual void sendMessage(const OutputMessage& message) = 0;

    virtual void subscribe(const std::string& tag, std::shared_ptr<MessageSubscriber> subscriber) = 0;

};

}
/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include <string>

#include "Message.h"

namespace sirius::drive::messenger
{

class MessageSubscriber
{

public:

    virtual ~MessageSubscriber() = default;

    virtual bool onMessageReceived( const InputMessage& message ) = 0;

};

}
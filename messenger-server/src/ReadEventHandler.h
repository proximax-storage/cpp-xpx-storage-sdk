/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include "messengerServer.pb.h"

namespace sirius::drive::messenger
{

class ReadEventHandler
{

public:

    virtual ~ReadEventHandler() = default;

    virtual void onRead( const std::optional<messengerServer::ClientMessage>& message ) = 0;

};

}
/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include <memory>

#include "StreamContext.h"

namespace sirius::drive::messenger
{

class ConnectionManager
{

public:

    virtual ~ConnectionManager() = default;

    virtual void onConnectionBroken( uint64_t id ) = 0;

    virtual void onConnectionEstablished( std::shared_ptr<StreamContext> context ) = 0;

};

}
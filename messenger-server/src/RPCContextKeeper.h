/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include <memory>

#include "RPCContext.h"
#include "ConnectionManager.h"

namespace sirius::drive::messenger {

class RPCContextKeeper {

private:

    uint64_t m_id;
    ConnectionManager& m_connectionManager;
    std::shared_ptr<RPCContext> m_context;

public:

    RPCContextKeeper( std::shared_ptr<RPCContext>&& context, uint64_t id, ConnectionManager& connectionManager );

    void onConnectionBrokenDetected();

    RPCContext& context();

};

}
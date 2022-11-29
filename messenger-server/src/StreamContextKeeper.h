/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include <memory>

#include "StreamContext.h"
#include "ConnectionManager.h"

namespace sirius::drive::messenger
{

class StreamContextKeeper
{

private:

    uint64_t m_id;
    ConnectionManager& m_connectionManager;
    std::shared_ptr<StreamContext> m_context;

public:

    StreamContextKeeper( std::shared_ptr<StreamContext>&& context, uint64_t id, ConnectionManager& connectionManager );

    ~StreamContextKeeper();

    void onConnectionBrokenDetected();

    StreamContext& context();

};

}
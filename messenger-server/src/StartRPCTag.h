/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "RPCTag.h"
#include "StreamContext.h"
#include "ConnectionManager.h"

namespace sirius::drive::messenger
{

class StartRPCTag
        : public RPCTag
{

private:

    std::weak_ptr<ConnectionManager> m_connectionManager;
    std::shared_ptr<StreamContext> m_context;

public:

    StartRPCTag( std::weak_ptr<ConnectionManager> connectionManager, std::shared_ptr<StreamContext> context );

    void process( bool ok ) override;

};

}
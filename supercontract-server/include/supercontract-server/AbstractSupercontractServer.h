/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include <ContextKeeper.h>
#include "ModificationsExecutor.h"

namespace sirius::drive::contract
{

class AbstractSupercontractServer {

public:

    virtual ~AbstractSupercontractServer() = default;

    virtual void run( std::weak_ptr<ContextKeeper> contextKeeper,
                      std::weak_ptr<ModificationsExecutor> executor ) = 0;

};

}

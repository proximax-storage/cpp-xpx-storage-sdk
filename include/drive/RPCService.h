/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include "IOContextProvider.h"
#include <grpcpp/server_builder.h>

namespace sirius::drive
{

class RPCService {

public:

    virtual ~RPCService() = default;

    virtual void registerService( grpc::ServerBuilder& builder) = 0;

    virtual void run(std::weak_ptr<IOContextProvider> contextKeeper) = 0;

};

}

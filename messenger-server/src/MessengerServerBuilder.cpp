/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include <messenger-server/MessengerServerBuilder.h>
#include "MessengerServer.h"

namespace sirius::drive::messenger
{

std::shared_ptr<RPCService> MessengerServerBuilder::build( std::weak_ptr<Messenger> messenger )
{
    return std::make_shared<MessengerServer>( std::move( messenger ));
}

}
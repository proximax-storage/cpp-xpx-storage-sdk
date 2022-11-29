/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include <drive/RPCService.h>
#include "Messenger.h"

namespace sirius::drive::messenger
{

class MessengerServerBuilder
{

public:

    std::shared_ptr<RPCService> build( std::weak_ptr<Messenger> );

};

}
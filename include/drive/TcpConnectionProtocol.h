/*
*** Copyright 2024 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include <stdint.h>

namespace sirius { namespace drive {

enum TcpRequestId : uint16_t
{
    get_peer_ip = 11
};

enum TcpResponseId : uint16_t
{
    peer_ip_response = 22
};

}}

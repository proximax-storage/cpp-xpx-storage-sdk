/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include <string>
#include "sirius/ionet/Node.h"

namespace sirius { namespace netio {
    class NodeConnector {
    public:
        NodeConnector(){}

        virtual void connect(const ionet::Node& node) = 0;

        virtual void shutdown() = 0;
    };

}} // namespace sirius::net
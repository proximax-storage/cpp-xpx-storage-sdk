#pragma once
#include <string>
#include "catapult/ionet/Node.h"

namespace catapult { namespace netio {
    class INodeConnector {
    public:
        INodeConnector(){}

        virtual void connect(const ionet::Node& node) = 0;

        virtual void shutdown() = 0;
    };

}} // namespace catapult::net
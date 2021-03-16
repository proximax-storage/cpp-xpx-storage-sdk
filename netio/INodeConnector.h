#pragma once
#include <string>
#include "sirius/ionet/Node.h"

namespace sirius { namespace netio {
    class INodeConnector {
    public:
        INodeConnector(){}

        virtual void connect(const ionet::Node& node) = 0;

        virtual void shutdown() = 0;
    };

}} // namespace sirius::net
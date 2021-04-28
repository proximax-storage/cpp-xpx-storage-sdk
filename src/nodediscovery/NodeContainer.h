/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once
#include <memory>
#include <unordered_set>
#include <boost/thread.hpp>
#include "crypto/KeyPair.h"
#include "connection/NodeConnector.h"
#include "ionet/PacketSocket.h"
#include "net/PeerConnectCode.h"
#include "ionet/Node.h"

namespace sirius { namespace nodediscovery {
        class NodeContainer {
        public:
            virtual ~NodeContainer(){};

        public:
            virtual void start(const std::vector<ionet::Node>&) = 0;
            virtual std::unordered_set<ionet::Node, ionet::NodeHasher> getActiveNodes() = 0;
        };

        std::shared_ptr<NodeContainer> CreateDefaultNodeContainer(
                const net::ConnectionSettings&,
                const crypto::KeyPair&,
                const uint64_t&,
                const uint64_t&);
    }
}

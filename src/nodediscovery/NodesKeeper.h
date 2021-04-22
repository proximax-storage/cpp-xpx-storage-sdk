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
        class NodesKeeper {
        public:
            explicit NodesKeeper(const net::ConnectionSettings&, const crypto::KeyPair&, const uint64_t&, const uint64_t&);

            void start(const ionet::Node& bootstrapNode);
            std::unordered_set<ionet::Node, ionet::NodeHasher> getActiveNodes();

        private:
            void connectionCallback(const ionet::Node&, net::PeerConnectCode, const std::shared_ptr<ionet::PacketSocket>&);
            void nodesConsumer(const ionet::NodeSet&);
            void timerTask(boost::system::error_code);

        private:
            boost::asio::io_context m_ioContext;
            boost::posix_time::milliseconds m_interval;
            boost::asio::deadline_timer m_timer;
            std::unordered_set<ionet::Node, ionet::NodeHasher> m_activeNodes;
            net::ConnectionSettings m_settings;
            const crypto::KeyPair& m_keyPair;
            uint64_t m_maxNodesCount;
        };
    }
}

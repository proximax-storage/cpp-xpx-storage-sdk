
#include "NodesKeeper.h"
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include "connection/SinglePeersRequestor.h"

namespace sirius { namespace nodediscovery {
        NodesKeeper::NodesKeeper(const net::ConnectionSettings& settings, const crypto::KeyPair& keyPair, const uint64_t& interval)
                : m_interval(boost::posix_time::milliseconds(interval))
                , m_timer(m_ioContext, m_interval)
                , m_pNodeConnector(connection::CreateDefaultNodeConnector(settings, keyPair, boost::bind(&NodesKeeper::connectionCallback, this, _1, _2, _3))) {
        }

        void NodesKeeper::start(const ionet::Node& bootstrapNode) {
            m_activeNodes.insert(bootstrapNode);
            m_timer.async_wait(boost::bind(&NodesKeeper::timerTask, this, _1));
            boost::thread thread(boost::bind(&boost::asio::io_context::run, &m_ioContext));
            thread.detach();
        }

        void NodesKeeper::connectionCallback(const ionet::Node& node, net::PeerConnectCode code, const std::shared_ptr<ionet::PacketSocket>& packet) {
            if (code == net::PeerConnectCode::Accepted) {
                ionet::NodePacketIoPair pair(node, packet);

                connection::SinglePeersRequestor requestor(pair, boost::bind(&NodesKeeper::nodesConsumer, this, _1));
                requestor.findPeersOfPeers().get();
            }
            else {
                auto isFound = m_activeNodes.find(node);
                if (isFound != m_activeNodes.end()) {
                    m_activeNodes.erase(isFound);
                }
            }
        }

        void NodesKeeper::nodesConsumer(const ionet::NodeSet& peers) {
            CATAPULT_LOG(info) << "Number of peers returned : " << peers.size();
            m_activeNodes.insert(peers.begin(), peers.end());
        }

        void NodesKeeper::timerTask(boost::system::error_code errorCode)
        {
            if (errorCode == boost::system::errc::success) {
                for (const auto& node : m_activeNodes) {
                    m_pNodeConnector->connect(node);
                }

                m_timer.expires_from_now(m_interval);
                m_timer.async_wait(boost::bind(&NodesKeeper::timerTask, this, _1));
            } else {
                CATAPULT_LOG(error) << errorCode.message();
            }
        }

        std::unordered_set<ionet::Node, ionet::NodeHasher> NodesKeeper::getActiveNodes() {
            return m_activeNodes;
        }
    }}
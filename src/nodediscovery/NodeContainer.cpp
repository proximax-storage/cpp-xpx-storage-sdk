/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "NodeContainer.h"
#include <boost/asio.hpp>
#include "connection/SinglePeersRequestor.h"

namespace sirius { namespace nodediscovery {
    class DefaultNodeContainer : public NodeContainer, public std::enable_shared_from_this<DefaultNodeContainer> {
    public:
        explicit DefaultNodeContainer(
                const net::ConnectionSettings& settings,
                const crypto::KeyPair& keyPair,
                const uint64_t& interval,
                const uint64_t& maxNodesCount)
                : m_interval(boost::posix_time::milliseconds(interval))
                , m_timer(m_ioContext, m_interval)
                , m_settings(settings)
                , m_keyPair(keyPair)
                , m_maxNodesCount(maxNodesCount) {
        }

        void start(const std::vector<ionet::Node>& bootstrapNodes) override {
            m_bootstrapNodes.insert(bootstrapNodes.begin(), bootstrapNodes.end());
            m_activeNodes.insert(bootstrapNodes.begin(), bootstrapNodes.end());
            m_timer.async_wait([pThis = shared_from_this()](auto& errorCode) {
                pThis->timerTask(errorCode);
            });

            boost::thread thread([ctx = &m_ioContext]() { ctx->run(); });
            thread.detach();
        }

        std::unordered_set<ionet::Node, ionet::NodeHasher> getActiveNodes() override {
            return m_activeNodes;
        }

    private:
        void connectionCallback(const ionet::Node& node, net::PeerConnectCode code, const std::shared_ptr<ionet::PacketSocket>& socket) {
            if (code == net::PeerConnectCode::Accepted) {
                ionet::NodePacketIoPair pair(node, socket);

                connection::SinglePeersRequestor requestor(pair, [pThis = shared_from_this()](auto& pees) {
                    pThis->nodesConsumer(pees);
                });
                requestor.findPeersOfPeers().get();
            }
            else {
                if (auto it = m_bootstrapNodes.find(node); it == m_bootstrapNodes.end()) {
                    if (it = m_activeNodes.find(node); it != m_activeNodes.end()) {
                        m_activeNodes.erase(it);
                    }
                }
            }
        }

        void nodesConsumer(const ionet::NodeSet& peers) {
            CATAPULT_LOG(info) << "Number of peers returned : " << peers.size();
            for (const auto& peer : peers) {
                if (m_activeNodes.size() < m_maxNodesCount) {
                    m_activeNodes.insert(peer);
                } else {
                    break;
                }
            }
        }

        void timerTask(boost::system::error_code errorCode) {
            if (errorCode == boost::system::errc::success) {
                if (m_activeNodes.empty()) {
                    m_activeNodes.insert(m_bootstrapNodes.begin(), m_bootstrapNodes.end());
                }

                for (const auto& node : m_activeNodes) {
                    auto connector = connection::CreateDefaultNodeConnector(m_settings, m_keyPair, [pThis = shared_from_this(), &node](auto code, auto socket) {
                        pThis->connectionCallback(node, code, socket);
                    });
                    connector->connect(node);
                    connector->close();
                }

                m_timer.expires_from_now(m_interval);
                m_timer.async_wait([pThis = shared_from_this()](auto& errorCode) {
                    pThis->timerTask(errorCode);
                });
            } else {
                throw std::runtime_error(errorCode.message());
            }
        }

    private:
        boost::asio::io_context m_ioContext;
        boost::posix_time::milliseconds m_interval;
        boost::asio::deadline_timer m_timer;
        std::unordered_set<ionet::Node, ionet::NodeHasher> m_activeNodes;
        std::unordered_set<ionet::Node, ionet::NodeHasher> m_bootstrapNodes;
        net::ConnectionSettings m_settings;
        const crypto::KeyPair& m_keyPair;
        uint64_t m_maxNodesCount;
    };

    std::shared_ptr<NodeContainer> CreateDefaultNodeContainer(
            const net::ConnectionSettings& settings,
            const crypto::KeyPair& keyPair,
            const uint64_t& interval,
            const uint64_t& maxNodesCount) {
        return std::make_shared<DefaultNodeContainer>(settings, keyPair, interval, maxNodesCount);
    }
}}
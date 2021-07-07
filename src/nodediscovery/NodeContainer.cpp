/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "NodeContainer.h"
#include <boost/asio.hpp>
#include <utils/Casting.h>
#include <crypto/KeyUtils.h>
#include "connection/SinglePeersRequestor.h"

namespace sirius::nodediscovery {
    class DefaultNodeContainer : public NodeContainer, public std::enable_shared_from_this<DefaultNodeContainer> {
    public:
        explicit DefaultNodeContainer(
                const net::ConnectionSettings& settings,
                const crypto::KeyPair& keyPair,
                const uint64_t& intervalMs,
                const uint64_t& maxNodesCount)
                : m_interval(boost::posix_time::milliseconds(intervalMs))
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
            std::lock_guard<std::mutex> lock(m_activeNodesMutex);
            return m_activeNodes;
        }

    private:
        void connectionCallback(const ionet::Node& node, net::PeerConnectCode code, const std::shared_ptr<ionet::PacketSocket>& socket) {
            std::stringstream publicKey;
            publicKey << crypto::FormatKey(node.identityKey());

            std::lock_guard<std::mutex> lock(m_activeNodesMutex);
            switch (code) {
                case net::PeerConnectCode::Socket_Error: {
                    CATAPULT_LOG(warning) << "Socket error: " << publicKey.str();
                    m_activeNodes.erase(node);
                    break;
                }

                case net::PeerConnectCode::Verify_Error: {
                    CATAPULT_LOG(warning) << "Verify error: " << publicKey.str();
                    m_activeNodes.erase(node);
                    break;
                }

                case net::PeerConnectCode::Timed_Out: {
                    CATAPULT_LOG(info) << "Peer has been removed by timeout: " << publicKey.str();
                    m_activeNodes.erase(node);
                    break;
                }

                case net::PeerConnectCode::Accepted: {
                    CATAPULT_LOG(info) << "Peer is accepted: " << publicKey.str();
                    ionet::NodePacketIoPair pair(node, socket);
                    connection::SinglePeersRequestor requestor(pair, [pThis = shared_from_this()](auto& peers) {
                        pThis->nodesConsumer(peers);
                    });
                    requestor.findPeersOfPeers().get();
                    break;
                }

                case net::PeerConnectCode::Already_Connected: {
                    CATAPULT_LOG(info) << "Peer is already connected: " << publicKey.str();
                    break;
                }

                default: {
                    CATAPULT_LOG(error) << "Unknown code: " << utils::to_underlying_type<net::PeerConnectCode>(code) << " peer has been removed: " << publicKey.str();
                    m_activeNodes.erase(node);
                }
            }
        }

        void nodesConsumer(const ionet::NodeSet& peers) {
            CATAPULT_LOG(info) << "Number of peers returned: " << peers.size();
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
        std::mutex m_activeNodesMutex;
    };

    std::shared_ptr<NodeContainer> CreateDefaultNodeContainer(
            const net::ConnectionSettings& settings,
            const crypto::KeyPair& keyPair,
            const uint64_t& intervalMs,
            const uint64_t& maxNodesCount) {
        return std::make_shared<DefaultNodeContainer>(settings, keyPair, intervalMs, maxNodesCount);
    }
}
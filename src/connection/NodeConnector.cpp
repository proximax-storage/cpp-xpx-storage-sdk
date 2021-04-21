/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "connection/NodeConnector.h"
#include "ionet/PacketSocket.h"
#include "ionet/SecurePacketSocketDecorator.h"
#include "net/VerifyPeer.h"
#include "utils/Logging.h"

namespace sirius { namespace connection {

    class DefaultNodeConnector : public NodeConnector {
    public:
        using ConnectCallback = consumer<const ionet::Node&, net::PeerConnectCode, const std::shared_ptr<ionet::PacketSocket>&>;
        using PacketSocketPointer = std::shared_ptr<ionet::PacketSocket>;

        DefaultNodeConnector(
				const net::ConnectionSettings& settings,
				const crypto::KeyPair& keyPair,
				const ConnectCallback& callback)
			: m_keyPair(keyPair)
			, m_callback(callback)
			, m_context()
			, m_settings(settings) {}

        virtual void connect(const ionet::Node& node) {
            auto cancel = ionet::Connect(
                    m_context,
                    m_settings.toSocketOptions(),
                    node.endpoint(),
                    [=,this](auto result, const auto& pConnectedSocket) {
                        if (ionet::ConnectResult::Connected != result) {
                            m_callback(node, net::PeerConnectCode::Socket_Error, nullptr);
                            return;
                        }

                        verify(node.identityKey(), node, pConnectedSocket);
                    });

            m_context.run();
        }

        virtual void close() {
            m_context.stop();
        }

    private:
        void verify(const Key& publicKey, const ionet::Node& node, const PacketSocketPointer& pConnectedSocket) {
            VerifiedPeerInfo serverPeerInfo{ publicKey, m_settings.OutgoingSecurityMode };

            VerifyServer(pConnectedSocket, serverPeerInfo, m_keyPair, [=,this](
                    auto verifyResult,
                    const auto& verifiedPeerInfo) {
                if (VerifyResult::Success != verifyResult) {
                    CATAPULT_LOG(warning) << "VerifyServer failed with " << verifyResult;
                    m_callback(node, net::PeerConnectCode::Verify_Error, nullptr);
                    return;
                }

                auto pSecuredSocket = secure(pConnectedSocket, verifiedPeerInfo);
                m_callback(node, net::PeerConnectCode::Accepted, pSecuredSocket);
            });
        }

        PacketSocketPointer secure(const PacketSocketPointer& pSocket, const VerifiedPeerInfo& peerInfo) {
            return Secure(pSocket, peerInfo.SecurityMode, m_keyPair, peerInfo.PublicKey, m_settings.MaxPacketDataSize);
        }

    private:
        const crypto::KeyPair&              m_keyPair;
        ConnectCallback                     m_callback;
        boost::asio::io_context             m_context;
        net::ConnectionSettings             m_settings;
    };

    std::shared_ptr<NodeConnector> CreateDefaultNodeConnector(
			const net::ConnectionSettings& settings,
			const crypto::KeyPair& keyPair,
			const NodeConnector::ConnectCallback& callback) {
        return std::make_shared<connection::DefaultNodeConnector>(settings, keyPair, callback);
    }
}}

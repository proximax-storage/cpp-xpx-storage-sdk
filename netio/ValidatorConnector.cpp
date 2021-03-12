#include <catapult/ionet/PacketSocket.h>
#include "ValidatorConnector.h"
#include "catapult/net/VerifyPeer.h"
#include "catapult/ionet/SecurePacketSocketDecorator.h"

using namespace catapult::net;
using namespace catapult::ionet;

namespace catapult { namespace netio {

    class ValidatorConnector : public INodeConnector {
    public:
        using ConnectCallback = consumer<net::PeerConnectCode, const std::shared_ptr<ionet::PacketSocket>&>;
        using PacketSocketPointer = std::shared_ptr<ionet::PacketSocket>;

        ValidatorConnector(const ionet::PacketSocketOptions& options,
                           const net::ConnectionSettings& settings,
                           const crypto::KeyPair& keyPair,
                           const ConnectCallback& callback)
                            : m_socketOptions(options)
                            , m_settings(settings)
                            , m_keyPair(keyPair)
                            , m_callback(callback)
                            , m_context() {}

        virtual void connect(const ionet::Node& node) {
            auto cancel = ionet::Connect(
                    m_context,
                    m_settings.toSocketOptions(),
                    node.endpoint(),
                    [=](auto result, const auto& pConnectedSocket) {
                        if (ionet::ConnectResult::Connected != result) {
                            m_callback(PeerConnectCode::Socket_Error, nullptr);
                            return;
                        }

                        verify(node.identityKey(), pConnectedSocket);
                    });

            m_context.run();
        }

        virtual void shutdown() {

        }

    private:
        void verify(const Key& publicKey, const PacketSocketPointer& pConnectedSocket) {
            VerifiedPeerInfo serverPeerInfo{ m_currentNode.identityKey(), m_settings.OutgoingSecurityMode };

            VerifyServer(pConnectedSocket, serverPeerInfo, m_keyPair, [=](
                    auto verifyResult,
                    const auto& verifiedPeerInfo) {
                if (VerifyResult::Success != verifyResult) {
                    // CATAPULT_LOG(warning) << "VerifyServer failed with " << verifyResult;
                    m_callback(PeerConnectCode::Verify_Error, nullptr);
                    return;
                }

                auto pSecuredSocket = secure(pConnectedSocket, verifiedPeerInfo);
                m_callback(PeerConnectCode::Accepted, pSecuredSocket);
            });
        }

        PacketSocketPointer secure(const PacketSocketPointer& pSocket, const VerifiedPeerInfo& peerInfo) {
            return Secure(pSocket, peerInfo.SecurityMode, m_keyPair, peerInfo.PublicKey, m_settings.MaxPacketDataSize);
        }

    private:
        ionet::Node                         m_currentNode;
        ionet::PacketSocketOptions          m_socketOptions;
        const crypto::KeyPair&              m_keyPair;
        ConnectCallback                     m_callback;
        boost::asio::io_context             m_context;
        net::ConnectionSettings             m_settings;
    };

    std::shared_ptr<INodeConnector> createValidatorConnector(const ionet::PacketSocketOptions& options,
                                                              const net::ConnectionSettings& settings,
                                                              const crypto::KeyPair& keyPair,
                                                              const ConnectCallback& callback) {

        return std::make_shared<netio::ValidatorConnector>(options, settings, keyPair, callback);
    }
}}
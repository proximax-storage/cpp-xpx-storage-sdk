#include "VerifyPeer.h"
#include "net/Challenge.h"
#include "utils/Logging.h"

namespace sirius { namespace connection {

#define DEFINE_ENUM VerifyResult
#define ENUM_LIST VERIFY_RESULT_LIST
#include "utils/MacroBasedEnum.h"
#undef ENUM_LIST
#undef DEFINE_ENUM

        // VerifyClient: the server (verifies client connections)
        // VerifyServer: the client (verifies server connections)
        // SERVER -> ServerChallengeRequest  -> CLIENT
        // SERVER <- ServerChallengeResponse <- CLIENT
        // SERVER -> ClientChallengeResponse -> CLIENT

    class VerifyServerHandler : public std::enable_shared_from_this<VerifyServerHandler> {
    public:
        VerifyServerHandler(const std::shared_ptr<ionet::PacketIo>& pIo,
                            const VerifiedPeerInfo& serverPeerInfo,
                            const crypto::KeyPair& keyPair,
                            const VerifyCallback& callback)
                            : m_pPacketIo(pIo)
                            , m_serverPeerInfo(serverPeerInfo)
                            , m_callback(callback)
							, m_keyPair(keyPair) {}

        void start() {
            m_pPacketIo->read([pThis = shared_from_this()](ionet::SocketOperationCode code, const ionet::Packet* pPacket) {
                pThis->handleServerChallengeRequestRead(code, pPacket);
            });
        }

        void handleServerChallengeRequestRead(ionet::SocketOperationCode code, const ionet::Packet* pPacket) {
            if (ionet::SocketOperationCode::Success != code)
                return invokeCallback(VerifyResult::Io_Error_ServerChallengeRequest);

            const auto* pRequest = ionet::CoercePacket<net::ServerChallengeRequest>(pPacket);
            if (!pRequest)
                return invokeCallback(VerifyResult::Malformed_Data);

            m_pRequest = GenerateServerChallengeResponse(*pRequest, m_keyPair, m_serverPeerInfo.SecurityMode);

            m_pPacketIo->write(ionet::PacketPayload(m_pRequest), [pThis = shared_from_this()](auto writeCode) {
                pThis->handleServerChallengeResponseWrite(writeCode);
            });
        }

        void handleServerChallengeResponseWrite(ionet::SocketOperationCode code) const {
            if (ionet::SocketOperationCode::Success != code)
                return invokeCallback(VerifyResult::Io_Error_ServerChallengeResponse);

            m_pPacketIo->read([pThis = shared_from_this()](auto readCode, const auto* pPacket) {
               pThis->handleClientChallengeReponseRead(readCode, pPacket);
            });
        }

        void handleClientChallengeReponseRead(ionet::SocketOperationCode code, const ionet::Packet* pPacket) const {
            if (ionet::SocketOperationCode::Success != code)
                return invokeCallback(VerifyResult::Io_Error_ClientChallengeResponse);

            const auto* pResponse = ionet::CoercePacket<net::ClientChallengeResponse>(pPacket);
            if (!pResponse)
                return invokeCallback(VerifyResult::Malformed_Data);

            auto isVerified = VerifyClientChallengeResponse(*pResponse, m_serverPeerInfo.PublicKey, m_pRequest->Challenge);
            invokeCallback(isVerified ? VerifyResult::Success: VerifyResult::Failure_Challenge);
        }

    private:
        void invokeCallback(VerifyResult result) const {
            CATAPULT_LOG(debug) << "VerifyServer completed with " << result;
            m_callback(result, m_serverPeerInfo);
        }

    private:
        std::shared_ptr<ionet::PacketIo>                   m_pPacketIo;
        VerifiedPeerInfo                            m_serverPeerInfo;
        VerifyCallback                              m_callback;
        const crypto::KeyPair&                      m_keyPair;
        std::shared_ptr<net::ServerChallengeResponse>    m_pRequest;
    };

    void VerifyServer(
            const std::shared_ptr<ionet::PacketIo>& pServerIo,
            const VerifiedPeerInfo& serverPeerInfo,
            const crypto::KeyPair& keyPair,
            const VerifyCallback& callback) {
        auto pHandler = std::make_shared<VerifyServerHandler>(pServerIo, serverPeerInfo, keyPair, callback);
        pHandler->start();
    }
}}
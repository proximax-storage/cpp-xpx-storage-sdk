#include "VerifyPeer.h"
//#include "PacketAdapter.h"
#include "catapult/net/Challenge.h"

using namespace catapult::ionet;
using namespace catapult::net;

namespace catapult { namespace netio {

    class VerifyServerHandler : public std::enable_shared_from_this<VerifyServerHandler> {
    public:
        VerifyServerHandler(const std::shared_ptr<PacketIo>& pIo,
                            const VerifiedPeerInfo& serverPeerInfo,
                            const crypto::KeyPair& keyPair,
                            const VerifyCallback& callback)
                            : m_packetIo(pIo)
                            , m_serverPeerInfo(serverPeerInfo)
                            , m_keyPair(keyPair)
                            , m_callback(callback) {}

        void start() {
            m_packetIo->read([pThis = shared_from_this()](ionet::SocketOperationCode code, const ionet::Packet* pPacket) {
                pThis->handleServerChallengeRequestRead(code, pPacket);
            });
        }

        void handleServerChallengeRequestRead(ionet::SocketOperationCode code, const ionet::Packet* pPacket) {
            if (ionet::SocketOperationCode::Success != code)
                return invokeCallback(VerifyResult::Io_Error_ServerChallengeRequest);

            const auto* pRequest = ionet::CoercePacket<ServerChallengeRequest>(pPacket);
            if (!pRequest)
                return invokeCallback(VerifyResult::Malformed_Data);

            m_pRequest = GenerateServerChallengeResponse(*pRequest, m_keyPair, m_serverPeerInfo.SecurityMode);

            m_packetIo->write(ionet::PacketPayload(m_pRequest), [pThis = shared_from_this()](auto writeCode) {
                pThis->handleServerChallengeResponseWrite(writeCode);
            });
        }

        void handleServerChallengeResponseWrite(ionet::SocketOperationCode code) const {
            if (ionet::SocketOperationCode::Success != code)
                return invokeCallback(VerifyResult::Io_Error_ServerChallengeResponse);

            m_packetIo->read([pThis = shared_from_this()](auto readCode, const auto* pPacket) {
               pThis->handleClientChallengeReponseRead(readCode, pPacket);
            });
        }

        void handleClientChallengeReponseRead(ionet::SocketOperationCode code, const ionet::Packet* pPacket) const {
            if (ionet::SocketOperationCode::Success != code)
                return invokeCallback(VerifyResult::Io_Error_ClientChallengeResponse);

            const auto* pResponse = ionet::CoercePacket<ClientChallengeResponse>(pPacket);
            if (!pResponse)
                return invokeCallback(VerifyResult::Malformed_Data);

            auto isVerified = VerifyClientChallengeResponse(*pResponse, m_serverPeerInfo.PublicKey, m_pRequest->Challenge);
            invokeCallback(isVerified ? VerifyResult::Success: VerifyResult::Failure_Challenge);
        }

    private:
        void invokeCallback(VerifyResult result) const {
            //CATAPULT_LOG(debug) << "VerifyServer completed with " << result;
            m_callback(result, m_serverPeerInfo);
        }

    private:
        std::shared_ptr<PacketIo>                   m_packetIo;
        VerifiedPeerInfo                            m_serverPeerInfo;
        VerifyCallback                              m_callback;
        const crypto::KeyPair&                      m_keyPair;
        std::shared_ptr<ServerChallengeResponse>    m_pRequest;
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
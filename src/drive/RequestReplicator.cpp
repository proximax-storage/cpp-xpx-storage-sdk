///*
//*** Copyright 2021 ProximaX Limited. All rights reserved.
//*** Use of this source code is governed by the Apache 2.0
//*** license that can be found in the LICENSE file.
//*/
//
//#pragma once
////#include "ApiTypes.h"
//#include "ionet/NetworkNode.h"
//#include "ionet/PacketIo.h"
//#include "ionet/PacketPayloadParser.h"
//#include "ionet/PacketEntityUtils.h"
//
//namespace sirius { namespace api {
//
//    struct ResponseFromReplicator
//    {
//        /// Size of the node.
//        uint32_t Size;
//
//        /// Version.
//        uint32_t Version;
//
//        /// Replicator public key).
//        Key IdentityKey;
//
//        /// Replicator public key).
//        Key TorrentInfoHash;
//
//        uint32_t ResponseCode;
//    public:
//        /// Calculates the real size of \a node.
//        static constexpr uint64_t CalculateRealSize(const ResponseFromReplicator& node) noexcept {
//            return sizeof(ResponseFromReplicator);
//        }
//    };
//
//    /// Dispatches requests to a replicator
//    class ReplicatorRequestDispatcher : std::enable_shared_from_this<ReplicatorRequestDispatcher> {
//    public:
//        /// Creates a remote request dispatcher around \a io.
//        explicit ReplicatorRequestDispatcher(ionet::PacketIo& io) : m_io(io)
//        {}
//
//    public:
//        template<typename TRequester>
//        void sendRequest( TRequester& requester, const std::shared_ptr<const ionet::Packet>& pPacket )
//        {
//            using ResultType = typename TRequester::ResponseType;
//            auto packetPayload = ionet::PacketPayload(pPacket);
//
//            m_io.write(packetPayload, [ &requester, self=shared_from_this() ] (auto code)
//            {
//                if (ionet::SocketOperationCode::Success != code)
//                    return requester.callback(RemoteChainResult::Write_Error);
//
//                self->m_io.read([ &requester, self=self ] (auto readCode, const auto* pResponsePacket)
//                {
//                    if (ionet::SocketOperationCode::Success != readCode)
//                        return requester.callback(RemoteChainResult::Read_Error);
//
//                    if ( pResponsePacket->Type != requester.resultType() ) {
//                        CATAPULT_LOG(warning)
//                                << "received packet of type " << pResponsePacket->Type
//                                << " but expected type " << requester.resultType();
//                        return requester.callback(RemoteChainResult::Malformed_Packet);
//                    }
//
//                    ResultType result;
//                    if (!self->tryParseResult(*pResponsePacket)) {
//                        CATAPULT_LOG(warning)
//                                << "unable to parse " << pResponsePacket->Type
//                                << " packet (size = " << pResponsePacket->Size << ")";
//                        return callback(RemoteChainResult::Malformed_Packet, ResultType());
//                    }
//
//                    return callback(RemoteChainResult::Success, std::move(result));
//                });
//            });
//        }
//
//    private:
//
//    template<typename TRequester>
//    bool tryParseResult(const ionet::Packet& packet) const
//    {
//        auto dataSize = ionet::CalculatePacketDataSize(packet);
//        if (!ionet::ContainsSingleEntity<ionet::NetworkNode>({ packet.Data(), dataSize }, ionet::IsSizeValid<ionet::NetworkNode>)) {
//            CATAPULT_LOG(warning) << "node packet is malformed with size " << dataSize;
//            return false;
//        }
//
//        //node = UnpackNode(reinterpret_cast<const ionet::NetworkNode&>(*packet.Data()));
//        return true;
//
//    }
//
//    private:
//        enum class RemoteChainResult {
//            Success,
//            Write_Error,
//            Read_Error,
//            Malformed_Packet
//        };
//
//        static constexpr const char* GetErrorMessage(RemoteChainResult result) {
//            switch (result) {
//            case RemoteChainResult::Write_Error:
//                return "write to remote node failed";
//            case RemoteChainResult::Read_Error:
//                return "read from remote node failed";
//            default:
//                return "remote node returned malformed packet";
//            }
//        }
//
//    private:
//        ionet::PacketIo& m_io;
//    };
//
//    void f( ionet::PacketIo& io )
//    {
//        ReplicatorRequestDispatcher dispatcher(io);
//        dispatcher.request();
//    }
//}}
//

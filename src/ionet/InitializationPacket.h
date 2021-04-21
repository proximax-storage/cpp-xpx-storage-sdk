/**
***
**/

#pragma once
#include "ionet/Packet.h"
#include "types.h"

namespace sirius { namespace ionet {
        /// Packet representing a initialization request from a client to a BC Validator.
        struct InitializationRequest : public ionet::Packet {
            static constexpr ionet::PacketType Packet_Type = ionet::PacketType::Client_Initialization;
        };

        /// Packet representing a initialization response from a BC Validator to a client.
        struct InitializationResponse : public ionet::Packet {
            static constexpr ionet::PacketType Packet_Type = ionet::PacketType::Client_Initialization;

            /// Replicator address
            std::string Host;

            /// Replicator port
            unsigned short Port;

            /// Challenge data that should be signed by the client.
            std::array<uint8_t,32> RootHash;
        };
}}

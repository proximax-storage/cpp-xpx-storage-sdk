#pragma once

#include "INodeConnector.h"
#include "sirius/ionet/PacketSocketOptions.h"
#include "sirius/crypto/KeyPair.h"
#include "sirius/ionet/PacketIo.h"
#include "sirius/net/PeerConnectCode.h"
#include "sirius/net/ConnectionSettings.h"

namespace sirius {
    namespace ionet {
        class PacketSocket;
    }
}

namespace sirius { namespace netio {

    using ConnectCallback = consumer<net::PeerConnectCode, const std::shared_ptr<ionet::PacketSocket>&>;

    std::shared_ptr<INodeConnector>
            createValidatorConnector(const ionet::PacketSocketOptions& options,
                                    const net::ConnectionSettings& settings,
                                    const crypto::KeyPair& keyPair,
                                    const ConnectCallback& callback);
}}
#pragma once

#include <catapult/ionet/PacketSocketOptions.h>
#include <catapult/crypto/KeyPair.h>
#include "INodeConnector.h"
#include "catapult/ionet/PacketIo.h"
#include "catapult/net/PeerConnectCode.h"
#include "catapult/net/ConnectionSettings.h"

namespace catapult {
    namespace ionet {
        class PacketSocket;
    }
}

namespace catapult { namespace netio {

    using ConnectCallback = consumer<net::PeerConnectCode, const std::shared_ptr<ionet::PacketSocket>&>;

    std::shared_ptr<INodeConnector>
            createValidatorConnector(const ionet::PacketSocketOptions& options,
                                    const net::ConnectionSettings& settings,
                                    const crypto::KeyPair& keyPair,
                                    const ConnectCallback& callback);
}}
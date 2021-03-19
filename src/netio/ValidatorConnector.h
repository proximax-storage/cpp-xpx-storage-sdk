/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include "NodeConnector.h"
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

    std::shared_ptr<NodeConnector>
            CreateDefaultNodeConnector(const ionet::PacketSocketOptions& options,
                                    const net::ConnectionSettings& settings,
                                    const crypto::KeyPair& keyPair,
                                    const NodeConnector::ConnectCallback& callback);
}}
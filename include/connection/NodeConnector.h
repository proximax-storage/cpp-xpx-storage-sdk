/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once
#include "crypto/KeyPair.h"
#include "ionet/PacketSocket.h"
#include "ionet/Node.h"
#include "net/ConnectionSettings.h"
#include "net/PeerConnectCode.h"
#include <string>

namespace sirius { namespace connection {

    class NodeConnector {
    public:
        using ConnectCallback = consumer<const ionet::Node&, net::PeerConnectCode, const std::shared_ptr<ionet::PacketSocket>&>;

    public:
        virtual ~NodeConnector(){}

	public:
        virtual void connect(const ionet::Node& node) = 0;
        virtual void close() = 0;
    };

	std::shared_ptr<NodeConnector> CreateDefaultNodeConnector(
		const net::ConnectionSettings& settings,
		const crypto::KeyPair& keyPair,
		const NodeConnector::ConnectCallback& callback);

}}
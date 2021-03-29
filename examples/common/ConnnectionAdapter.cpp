/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#include "crypto/KeyPair.h"
#include "connection/NodeConnector.h"
#include "crypto/KeyUtils.h"
#include "ConnnectionAdapter.h"

namespace sirius { namespace sdk { namespace examples {

    std::shared_ptr<connection::NodeConnector> ConnectNode(const ionet::Node& node, const connection::NodeConnector::ConnectCallback& callback) {
        ionet::PacketSocketOptions options;
        options.WorkingBufferSize =  524288;
        options.WorkingBufferSensitivity = 100;
        options.MaxPacketDataSize = 157286400;

        std::string bootKey = "CB9183E99CB2D027E6905E5F6DF747EAEF17C53615F0CB76E28CB8A4EE2FCDEB";
        auto kePair = crypto::KeyPair::FromString(bootKey);

        net::ConnectionSettings settings;
        settings.Timeout = utils::TimeSpan::FromSeconds(10);
        settings.SocketWorkingBufferSize =  utils::FileSize::FromKilobytes(512);
        settings.SocketWorkingBufferSensitivity = 100;
        settings.MaxPacketDataSize = utils::FileSize::FromMegabytes(150);

        settings.OutgoingSecurityMode = static_cast<ionet::ConnectionSecurityMode>(1);
        settings.IncomingSecurityModes = static_cast<ionet::ConnectionSecurityMode>(1);

        auto connector = connection::CreateDefaultNodeConnector(settings, kePair, callback);

        connector->connect(node);
        return std::move(connector);
    }
}}}
/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "connection/NodeConnector.h"
#include "crypto/KeyPair.h"
#include "crypto/KeyUtils.h"
#include "ionet/Node.h"
#include "net/PeerConnectCode.h"
#include <iostream>

using namespace sirius;

int main() {
    std::string bootKey = "CB9183E99CB2D027E6905E5F6DF747EAEF17C53615F0CB76E28CB8A4EE2FCDEB";
    auto kePair = crypto::KeyPair::FromString(bootKey);

    ionet::NodeEndpoint endpoint;
    endpoint.Port = 7906;
    endpoint.Host = "127.0.0.1";

    std::string nodeKey = "7D31F73B9C73D1381F331659970B1D284099E2B4A65BD6CDCA33D0D10222255B";
    auto key = crypto::ParseKey(nodeKey);

    auto node = ionet::Node(key, endpoint);

    net::ConnectionSettings settings;
    settings.Timeout = utils::TimeSpan::FromSeconds(10);
    settings.SocketWorkingBufferSize =  utils::FileSize::FromKilobytes(512);
    settings.SocketWorkingBufferSensitivity = 100;
    settings.MaxPacketDataSize = utils::FileSize::FromMegabytes(150);

    settings.OutgoingSecurityMode = static_cast<ionet::ConnectionSecurityMode>(1);
    settings.IncomingSecurityModes = static_cast<ionet::ConnectionSecurityMode>(1);

    auto connector = connection::CreateDefaultNodeConnector(settings, kePair,[=](auto, auto code, auto){
        if(code == net::PeerConnectCode::Accepted)
            std::cout << "Node Connection Accepted" << std::endl;
        else
            std::cout << "Node Connection failed " << std::endl;
    });

    connector->connect(node);

    getchar();

    return 0;
}

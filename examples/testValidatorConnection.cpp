#include <iostream>
#include <boost/asio.hpp>
#include "sirius/crypto/KeyPair.h"
#include "netio/ValidatorConnector.h"
#include "sirius/ionet/Node.h"
#include "sirius/crypto/KeyUtils.h"
#include "sirius/net/PeerConnectCode.h"

using namespace sirius;

int main() {
    ionet::PacketSocketOptions options;
    options.WorkingBufferSize =  524288;
    options.WorkingBufferSensitivity = 100;
    options.MaxPacketDataSize = 157286400;

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

    auto connector = netio::createValidatorConnector(options, settings, kePair,[=](auto code, auto packet){
        if(code == net::PeerConnectCode::Accepted)
            std::cout << "Node Connection Accepted" << std::endl;
        else
            std::cout << "Node Connection failed " << std::endl;
    });

    connector->connect(node);

    getchar();

    return 0;
}

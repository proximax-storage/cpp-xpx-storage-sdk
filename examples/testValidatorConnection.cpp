#include <iostream>
#include <boost/asio.hpp>
#include "catapult/crypto/KeyPair.h"
#include "netio/ValidatorConnector.h"
#include "catapult/ionet/Node.h"
#include "catapult/crypto/KeyUtils.h"
#include "catapult/net/PeerConnectCode.h"

using namespace catapult;

int main() {
    ionet::PacketSocketOptions options;
    options.WorkingBufferSize =  524288;
    options.WorkingBufferSensitivity = 100;
    options.MaxPacketDataSize = 157286400;

    std::string peer1Bootkey = "14BAB567511DA5B14B76920238C644809F9F1B89EBF4EA3E4CA391608B3AC375";
    auto kePair = crypto::KeyPair::FromString(peer1Bootkey);

    ionet::NodeEndpoint endpoint;
    endpoint.Port = 7903;
    endpoint.Host = "127.0.0.1";

    std::string nodeKey = "C5B8C1D23688622F1A0139E90E7C8863FA66A1EAE1FEA8F52F087959F7468D96";
    auto key = crypto::ParseKey(nodeKey);

    auto node = ionet::Node(key, endpoint);

    net::ConnectionSettings settings;
    settings.Timeout = utils::TimeSpan::FromSeconds(11);
    settings.SocketWorkingBufferSize =  utils::FileSize::FromBytes(512);
    settings.SocketWorkingBufferSensitivity = 100;
    settings.MaxPacketDataSize = utils::FileSize::FromKilobytes(12);

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

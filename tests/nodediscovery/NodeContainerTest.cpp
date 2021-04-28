#include <gtest/gtest.h>
#include <crypto/KeyUtils.h>
#include "nodediscovery/NodeContainer.h"

namespace cpp_xpx_storage_sdk::tests {

#define TEST_CLASS NodeContainer

    TEST(TEST_CLASS, Start) {
        sirius::ionet::NodeEndpoint endpoint;
        endpoint.Host = "127.0.0.1";
        endpoint.Port = 7903;

        const sirius::crypto::KeyPair keyPair = sirius::crypto::KeyPair::FromString("415ABF3AA04493587AD0B3C6B594F369F74F4055CFBD2BF028AE29EA3C1C6BD3");
        auto nodeContainer = sirius::nodediscovery::CreateDefaultNodeContainer(sirius::net::ConnectionSettings(), keyPair, 5000, 10);

        const sirius::Key identityKey = sirius::crypto::ParseKey("E8D4B7BEB2A531ECA8CC7FD93F79A4C828C24BE33F99CF7C5609FF5CE14605F4");
        std::vector<sirius::ionet::Node> bootstrapNodes{sirius::ionet::Node(identityKey, endpoint)};
        nodeContainer->start(bootstrapNodes);

        EXPECT_FALSE(nodeContainer->getActiveNodes().empty());
    }
}
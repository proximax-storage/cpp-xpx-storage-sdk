#include <gtest/gtest.h>
#include <crypto/KeyUtils.h>
#include "nodediscovery/NodesKeeper.h"

namespace cpp_xpx_storage_sdk::tests {

#define TEST_CLASS NodesKeeper

    TEST(TEST_CLASS, Start) {
        sirius::ionet::NodeEndpoint endpoint;
        endpoint.Host = "127.0.0.1";
        endpoint.Port = 7903;

        const sirius::Key identityKey = sirius::crypto::ParseKey("E8D4B7BEB2A531ECA8CC7FD93F79A4C828C24BE33F99CF7C5609FF5CE14605F4");
        sirius::ionet::Node bootstrapNode(identityKey, endpoint);

        const sirius::crypto::KeyPair keyPair = sirius::crypto::KeyPair::FromString("415ABF3AA04493587AD0B3C6B594F369F74F4055CFBD2BF028AE29EA3C1C6BD3");
        sirius::nodediscovery::NodesKeeper nodesKeeper(sirius::net::ConnectionSettings(), keyPair, 2000);
        nodesKeeper.start(bootstrapNode);

        char a;
        int ret = std::scanf("%c\n", &a);
        (void) ret; // ignore

        EXPECT_EQ (1, 1);
    }
}
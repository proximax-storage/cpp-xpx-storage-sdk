/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include <iostream>
#include "connection/SinglePeersRequestor.h"
#include "ionet/Node.h"
#include "crypto/KeyUtils.h"
#include "net/PeerConnectCode.h"
#include "common/ConnnectionAdapter.h"

using namespace sirius;

int main() {

    ionet::NodeEndpoint endpoint;
    endpoint.Port = 7906;
    endpoint.Host = "127.0.0.1";

    std::string nodeKey = "7D31F73B9C73D1381F331659970B1D284099E2B4A65BD6CDCA33D0D10222255B";
    auto key = crypto::ParseKey(nodeKey);

    auto node = ionet::Node(key, endpoint);
    auto connector = sdk::examples::ConnectNode(node, [n = node](auto, auto code, auto packet){
        if(code != net::PeerConnectCode::Accepted) {
            std::cout << "Node Connection failed " << std::endl;
            return;
        }

        ionet::NodePacketIoPair pair(n, packet);
        connection::SinglePeersRequestor requestor(pair, [](const ionet::NodeSet& peers){
            std::cout << "Number of peers returned : " << peers.size() ;
        });

        requestor.findPeersOfPeers().get();
    });

    return 0;
}

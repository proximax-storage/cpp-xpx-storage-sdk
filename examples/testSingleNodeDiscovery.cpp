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
    endpoint.Port = 7904;
    endpoint.Host = "127.0.0.1";

    std::string nodeKey = "E8D4B7BEB2A531ECA8CC7FD93F79A4C828C24BE33F99CF7C5609FF5CE14605F4";
    auto key = crypto::ParseKey(nodeKey);

    auto node = ionet::Node(key, endpoint);
    auto connector = sdk::examples::ConnectNode(node, [n = node](auto code, auto packet){
        if(code != net::PeerConnectCode::Accepted) {
            std::cout << "Node Connection failed " << std::endl;
            return;
        }

        ionet::NodePacketIoPair pair(n, packet);
        connection::SinglePeersRequestor requestor(pair, [=](const ionet::NodeSet& peers){
            std::cout << "Number of peers returned : " << peers.size() ;
        });

        requestor.findPeersOfPeers();
    });

    getchar();

    return 0;
}

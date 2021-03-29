/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include "ionet/NodeInteractionResult.h"
#include "ionet/Node.h"
#include "functions.h"
#include "ionet/NodePacketIoPair.h"
#include "thread/Future.h"

namespace sirius { namespace netio {

    /// Creates a single node peers requestor.
    class SinglePeersRequestor {
    private:
        using NodesConsumer = consumer<const ionet::NodeSet&>;
        using RemoteApiResults = std::vector<ionet::NodeInteractionResult>;

    public:
        /// Creates a requestor around \a nodePacketIoPair, which contains a node and an io. Forwards found nodes to \a nodesConsumer.
        explicit SinglePeersRequestor(ionet::NodePacketIoPair& nodePacketIoPair, const NodesConsumer& nodesConsumer);

        /// Finds and forwards peers of a node
        thread::future<RemoteApiResults> findPeersOfPeers( ) const;

    private:
        ionet::NodePacketIoPair& m_nodePacketIoPair;
        NodesConsumer m_nodesConsumer;
    };
}}




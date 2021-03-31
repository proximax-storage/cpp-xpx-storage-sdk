
#include "nodediscovery/RemoteNodeApi.h"
#include "connection/SinglePeersRequestor.h"

namespace sirius { namespace connection {
    SinglePeersRequestor::SinglePeersRequestor(ionet::NodePacketIoPair& nodePacketIoPair, const NodesConsumer& nodesConsumer)
        : m_nodePacketIoPair(nodePacketIoPair)
        , m_nodesConsumer(nodesConsumer)
    {
    }

    thread::future<SinglePeersRequestor::RemoteApiResults> SinglePeersRequestor::findPeersOfPeers( ) const {
        auto peersInfoFuture = api::CreateRemoteNodeApi(*m_nodePacketIoPair.io().get())->peersInfo();
        const auto& identityKey = m_nodePacketIoPair.node().identityKey();
        peersInfoFuture.then([nodesConsumer = m_nodesConsumer, packetIoPair = m_nodePacketIoPair, identityKey](auto&& nodesFuture) {
            try {
                auto nodes = nodesFuture.get();
                CATAPULT_LOG(debug) << "partner node " << packetIoPair.node() << " returned " << nodes.size() << " peers";
                nodesConsumer(nodes);
                return ionet::NodeInteractionResult(identityKey, ionet::NodeInteractionResultCode::Success);
            } catch (const catapult_runtime_error& e) {
                CATAPULT_LOG(warning) << "exception thrown while requesting peers: " << e.what();
                return ionet::NodeInteractionResult(identityKey, ionet::NodeInteractionResultCode::Failure);
            }
        });

        return thread::make_ready_future(std::vector<ionet::NodeInteractionResult>());
    }
}}
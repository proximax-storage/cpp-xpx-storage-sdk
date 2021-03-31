/**
*** Copyright (c) 2016-present,
*** Jaguar0625, gimre, BloodyRookie, Tech Bureau, Corp. All rights reserved.
***
*** This file is part of Catapult.
***
*** Catapult is free software: you can redistribute it and/or modify
*** it under the terms of the GNU Lesser General Public License as published by
*** the Free Software Foundation, either version 3 of the License, or
*** (at your option) any later version.
***
*** Catapult is distributed in the hope that it will be useful,
*** but WITHOUT ANY WARRANTY; without even the implied warranty of
*** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*** GNU Lesser General Public License for more details.
***
*** You should have received a copy of the GNU Lesser General Public License
*** along with Catapult. If not, see <http://www.gnu.org/licenses/>.
**/

#include "NodePingUtils.h"
#include "ionet/NetworkNode.h"
#include "ionet/PacketEntityUtils.h"

namespace sirius { namespace nodediscovery {

	bool TryParseNodePacket(const ionet::Packet& packet, ionet::Node& node) {
		auto dataSize = ionet::CalculatePacketDataSize(packet);
		if (!ionet::ContainsSingleEntity<ionet::NetworkNode>({ packet.Data(), dataSize }, ionet::IsSizeValid<ionet::NetworkNode>)) {
			CATAPULT_LOG(warning) << "node packet is malformed with size " << dataSize;
			return false;
		}

		node = UnpackNode(reinterpret_cast<const ionet::NetworkNode&>(*packet.Data()));
		return true;
	}

	bool TryParseNodesPacket(const ionet::Packet& packet, ionet::NodeSet& nodes) {
		auto range = ionet::ExtractEntitiesFromPacket<ionet::NetworkNode>(packet, ionet::IsSizeValid<ionet::NetworkNode>);
		if (range.empty() && sizeof(ionet::PacketHeader) != packet.Size) {
			CATAPULT_LOG(warning) << "rejecting empty range: " << packet;
			return false;
		}

		for (const auto& networkNode : range)
			nodes.emplace(ionet::UnpackNode(networkNode));

		return true;
	}
}}

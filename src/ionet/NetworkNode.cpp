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

#include "ionet/NetworkNode.h"
#include "model/EntityPtr.h"

namespace sirius { namespace ionet {

	Node UnpackNode(const NetworkNode& networkNode) {
		const auto* pNetworkNodeData = reinterpret_cast<const char*>(&networkNode + 1);

		auto endpoint = NodeEndpoint();
		endpoint.Port = networkNode.Port;
		endpoint.Host = std::string(pNetworkNodeData, networkNode.HostSize);
		pNetworkNodeData += networkNode.HostSize;

		auto metadata = NodeMetadata();
		metadata.NetworkIdentifier = networkNode.NetworkIdentifier;
		metadata.Name = std::string(pNetworkNodeData, networkNode.FriendlyNameSize);
		metadata.Version = networkNode.Version;
		metadata.Roles = networkNode.Roles;

		return Node(networkNode.IdentityKey, endpoint, metadata);
	}
}}

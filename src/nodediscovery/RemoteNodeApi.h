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

#pragma once
#include "ionet/Node.h"
#include "thread/Future.h"

namespace sirius { namespace ionet { class PacketIo; } }

namespace sirius { namespace api {

	/// An api for retrieving node information from a remote node.
	class PLUGIN_API RemoteNodeApi {
	public:
		virtual ~RemoteNodeApi() = default;

	public:
		/// Gets information about the node.
		virtual thread::future<ionet::Node> nodeInfo() const = 0;

		/// Gets information about the node's peers.
		virtual thread::future<ionet::NodeSet> peersInfo() const = 0;
	};

	/// Creates a node api for interacting with a remote node with the specified \a io.
	PLUGIN_API std::unique_ptr<RemoteNodeApi> CreateRemoteNodeApi(ionet::PacketIo& io);
}}

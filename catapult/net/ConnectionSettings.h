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
#include "catapult/ionet/ConnectionSecurityMode.h"
#include "catapult/ionet/PacketSocketOptions.h"
//#include "catapult/model/NetworkInfo.h"
#include "catapult/utils/FileSize.h"
#include "catapult/utils/TimeSpan.h"

namespace catapult { namespace net {

	/// Settings used to configure connections.
	struct ConnectionSettings {
	public:
		/// Creates default settings.
		ConnectionSettings()
				: Timeout(utils::TimeSpan::FromSeconds(10))
				, SocketWorkingBufferSize(utils::FileSize::FromKilobytes(4))
				, SocketWorkingBufferSensitivity(0) // memory reclamation disabled
				, MaxPacketDataSize(utils::FileSize::FromMegabytes(100))
				, OutgoingSecurityMode(ionet::ConnectionSecurityMode::None)
				, IncomingSecurityModes(ionet::ConnectionSecurityMode::None)
		{}

	public:
		/// Connection timeout.
		utils::TimeSpan Timeout;

		/// Socket working buffer size.
		utils::FileSize SocketWorkingBufferSize;

		/// Socket working buffer sensitivity.
		size_t SocketWorkingBufferSensitivity;

		/// Maximum packet data size.
		utils::FileSize MaxPacketDataSize;

		/// Security mode of outgoing connections initiated by this node.
		ionet::ConnectionSecurityMode OutgoingSecurityMode;

		/// Accepted security modes of incoming connections initiated by other nodes.
		ionet::ConnectionSecurityMode IncomingSecurityModes;

	public:
		/// Gets the packet socket options represented by the configured settings.
		ionet::PacketSocketOptions toSocketOptions() const {
			ionet::PacketSocketOptions options;
			options.WorkingBufferSize = SocketWorkingBufferSize.bytes();
			options.WorkingBufferSensitivity = SocketWorkingBufferSensitivity;
			options.MaxPacketDataSize = MaxPacketDataSize.bytes();
			return options;
		}
	};
}}

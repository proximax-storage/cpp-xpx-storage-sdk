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
#include "PacketType.h"
#include <iosfwd>

namespace sirius { namespace ionet {

#pragma pack(push, 1)

	/// A packet header that all transferable information is expected to have.
	struct PLUGIN_API PacketHeader {
		/// Size of the packet.
		uint32_t Size;

		/// Type of the packet.
		PacketType Type;
	};

#pragma pack(pop)

	/// Determines if \a header indicates a valid packet data size no greater than \a maxPacketDataSize.
	constexpr bool IsPacketDataSizeValid(const PacketHeader& header, size_t maxPacketDataSize) {
		return header.Size >= sizeof(PacketHeader) && (header.Size - sizeof(PacketHeader)) <= maxPacketDataSize;
	}

	/// Insertion operator for outputting \a header to \a out.
	PLUGIN_API std::ostream& operator<<(std::ostream& out, const PacketHeader& header);
}}

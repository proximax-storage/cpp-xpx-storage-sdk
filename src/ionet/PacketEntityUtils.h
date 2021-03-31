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
#include "Packet.h"
#include "PacketPayloadParser.h"
#include "model/EntityRange.h"
#include "model/EntityPtr.h"

namespace sirius { namespace ionet {

	/// Calculates the data size of \a packet.
	inline size_t CalculatePacketDataSize(const Packet& packet) {
		constexpr auto Min_Size = sizeof(PacketHeader);
		if (packet.Size <= Min_Size) {
			if (packet.Size < Min_Size)
				CATAPULT_LOG(warning) << "packet size (" << packet.Size << ") must be at least " << Min_Size;

			return 0;
		}

		return packet.Size - Min_Size;
	}

	/// Checks the real size of \a entity against its reported size and returns \c true if the sizes match.
	template<typename TEntity>
	bool IsSizeValid(const TEntity& entity) {
		return TEntity::CalculateRealSize(entity) == entity.Size;
	}

	/// Extracts entities from \a packet with a validity check (\a isValid).
	/// \note If the packet is invalid and/or contains partial entities, the returned range will be empty.
	template<typename TEntity, typename TIsValidPredicate>
	model::EntityRange<TEntity> ExtractEntitiesFromPacket(const Packet& packet, TIsValidPredicate isValid) {
		auto dataSize = CalculatePacketDataSize(packet);
		auto offsets = ExtractEntityOffsets<TEntity>({ packet.Data(), dataSize }, isValid);
		return offsets.empty()
				? model::EntityRange<TEntity>()
				: model::EntityRange<TEntity>::CopyVariable(packet.Data(), dataSize, offsets);
	}
}}

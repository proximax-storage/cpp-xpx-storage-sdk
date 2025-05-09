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
#include "NodeInteractionResultCode.h"
#include "types.h"

namespace sirius { namespace ionet {

	/// Result from a node interaction.
	struct PLUGIN_API NodeInteractionResult {
	public:
		/// Creates a default node interaction result.
		NodeInteractionResult() : NodeInteractionResult(Key(), NodeInteractionResultCode::None)
		{}

		/// Creates a node interaction result around \a identityKey and \a code.
		NodeInteractionResult(const Key& identityKey, NodeInteractionResultCode code)
				: IdentityKey(identityKey)
				, Code(code)
		{}

	public:
		/// Identity key of the remote node.
		Key IdentityKey;

		/// Interaction result code.
		NodeInteractionResultCode Code;
	};
}}

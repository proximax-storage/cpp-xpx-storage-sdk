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

#include "model/NetworkInfo.h"
#include "utils/ConfigurationValueParsers.h"
#include "utils/MacroBasedEnumIncludes.h"

namespace sirius { namespace model {

#define DEFINE_ENUM NetworkIdentifier
#define EXPLICIT_VALUE_ENUM
#define ENUM_LIST NETWORK_IDENTIFIER_LIST
#include "utils/MacroBasedEnum.h"
#undef ENUM_LIST
#undef EXPLICIT_VALUE_ENUM
#undef DEFINE_ENUM

	namespace {
		const std::array<std::pair<const char*, NetworkIdentifier>, 6> String_To_Network_Identifier_Pairs{{
			{ "mijin", NetworkIdentifier::Mijin },
			{ "mijin-test", NetworkIdentifier::Mijin_Test },
			{ "private", NetworkIdentifier::Private },
			{ "private-test", NetworkIdentifier::Private_Test },
			{ "public", NetworkIdentifier::Public },
			{ "public-test", NetworkIdentifier::Public_Test }
		}};
	}

	bool TryParseValue(const std::string& networkName, NetworkIdentifier& networkIdentifier) {
		if (utils::TryParseEnumValue(String_To_Network_Identifier_Pairs, networkName, networkIdentifier))
			return true;

		uint8_t rawNetworkIdentifier;
		if (!utils::TryParseValue(networkName, rawNetworkIdentifier))
			return false;

		networkIdentifier = static_cast<NetworkIdentifier>(rawNetworkIdentifier);
		return true;
	}
}}

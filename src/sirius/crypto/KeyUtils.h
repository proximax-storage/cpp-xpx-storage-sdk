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
#include "sirius/utils/HexFormatter.h"
#include "sirius/types.h"
#include <sstream>

namespace sirius { namespace crypto { class PrivateKey; } }

namespace sirius { namespace crypto {

	/// Formats a public \a key for printing.
	utils::ContainerHexFormatter<Key::const_iterator> FormatKey(const Key& key);

	/// Formats a private \a key for printing.
	utils::ContainerHexFormatter<Key::const_iterator> FormatKey(const PrivateKey& key);

	/// Formats a string \a key for printing.
	utils::ContainerHexFormatter<const uint8_t*> FormatKey(const std::string& key);

	/// Parses a key from a string (\a keyString) and returns the result.
	Key ParseKey(const std::string& keyString);

	/// Returns \c true if \a str represents a valid public key, \c false otherwise.
	bool IsValidKeyString(const std::string& str);

	/// Returns string representation of Key.
	template<typename T>
	std::string FormatKeyAsString(const T& key) {
		std::ostringstream out;
		out << FormatKey(key);
		return out.str();
	}
}}

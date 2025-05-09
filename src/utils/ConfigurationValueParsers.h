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
#include "utils/Logging.h"
#include "types.h"
#include "utils/Hashers.h"
#include <array>
#include <unordered_set>

namespace sirius {
	namespace utils {
		class BlockSpan;
		class FileSize;
		class TimeSpan;
	}
}

namespace sirius { namespace utils {

	/// Tries to parse \a str into a log level (\a parsedValue).
	PLUGIN_API bool TryParseValue(const std::string& str, LogLevel& parsedValue);

	/// Tries to parse \a str into a log sink type (\a parsedValue).
	PLUGIN_API bool TryParseValue(const std::string& str, LogSinkType& parsedValue);

	/// Tries to parse \a str into a log color mode (\a parsedValue).
	PLUGIN_API bool TryParseValue(const std::string& str, LogColorMode& parsedValue);

	/// Tries to parse \a str into a boolean (\a parsedValue).
	PLUGIN_API bool TryParseValue(const std::string& str, bool& parsedValue);

	/// Tries to parse \a str into a uint8_t (\a parsedValue).
	PLUGIN_API bool TryParseValue(const std::string& str, uint8_t& parsedValue);

	/// Tries to parse \a str into a uint16_t (\a parsedValue).
	PLUGIN_API bool TryParseValue(const std::string& str, uint16_t& parsedValue);

	/// Tries to parse \a str into a uint32_t (\a parsedValue).
	PLUGIN_API bool TryParseValue(const std::string& str, uint32_t& parsedValue);

	/// Tries to parse \a str into a uint64_t (\a parsedValue).
	PLUGIN_API bool TryParseValue(const std::string& str, uint64_t& parsedValue);

	/// Tries to parse \a str into a double (\a parsedValue).
	PLUGIN_API bool TryParseValue(const std::string& str, double& parsedValue);

	/// Tries to parse \a str into a TimeSpan (\a parsedValue).
	PLUGIN_API bool TryParseValue(const std::string& str, TimeSpan& parsedValue);

	/// Tries to parse \a str into a BlockSpan (\a parsedValue).
	PLUGIN_API bool TryParseValue(const std::string& str, BlockSpan& parsedValue);

	/// Tries to parse \a str into a FileSize (\a parsedValue).
	PLUGIN_API bool TryParseValue(const std::string& str, FileSize& parsedValue);

	/// Tries to parse \a str into a Key (\a parsedValue).
	PLUGIN_API bool TryParseValue(const std::string& str, Key& parsedValue);

	/// Tries to parse \a str into a Hash256 (\a parsedValue).
	PLUGIN_API bool TryParseValue(const std::string& str, Hash256& parsedValue);

	/// Tries to parse \a str into a GenerationHash (\a parsedValue).
	PLUGIN_API bool TryParseValue(const std::string& str, GenerationHash& parsedValue);

	/// Tries to parse \a str into a string (\a parsedValue).
	/// \note This function just copies \a str into \a parsedValue.
	PLUGIN_API bool TryParseValue(const std::string& str, std::string& parsedValue);

	/// Tries to parse \a str into a set of strings (\a parsedValue).
	/// \note \a str is expected to be comma separated
	PLUGIN_API bool TryParseValue(const std::string& str, std::unordered_set<std::string>& parsedValue);

	/// Tries to parse \a str into an enum value (\a parsedValue) given a mapping of strings to values (\a stringToValueMapping).
	template<typename T, size_t N>
	PLUGIN_API bool TryParseEnumValue(const std::array<std::pair<const char*, T>, N>& stringToValueMapping, const std::string& str, T& parsedValue) {
		auto iter = std::find_if(stringToValueMapping.cbegin(), stringToValueMapping.cend(), [&str](const auto& pair) {
			return pair.first == str;
		});

		if (stringToValueMapping.cend() == iter)
			return false;

		parsedValue = iter->second;
		return true;
	}

	/// Tries to parse \a str into a bitwise enum value (\a parsedValue) given a mapping of strings to values (\a stringToValueMapping).
	template<typename T, size_t N>
	PLUGIN_API bool TryParseBitwiseEnumValue(
			const std::array<std::pair<const char*, T>, N>& stringToValueMapping,
			const std::string& str,
			T& parsedValues) {
		std::unordered_set<std::string> parts;
		if (!TryParseValue(str, parts))
			return false;

		T values = static_cast<T>(0);
		for (const auto& part : parts) {
			T value;
			if (!TryParseEnumValue(stringToValueMapping, part, value))
				return false;

			values |= value;
		}

		parsedValues = values;
		return true;
	}
}}

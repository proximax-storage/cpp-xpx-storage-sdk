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
#include "Hashers.h"
#include "types.h"
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace sirius { namespace utils {

	/// Functor for hashing an array pointer.
	template<typename TArray>
	struct ArrayPointerHasher {
		size_t operator()(const TArray* pArray) const {
			return ArrayHasher<TArray>()(*pArray);
		}
	};

	/// Functor for comparing two array pointers.
	template<typename TArray>
	struct ArrayPointerEquality {
		bool operator()(const TArray* pLhs, const TArray* pRhs) const {
			return *pLhs == *pRhs;
		}
	};

	/// A set of arrays.
	template<typename TArray>
	using ArraySet = std::unordered_set<TArray, ArrayHasher<TArray>>;

	/// A set of array pointers.
	template<typename TArray>
	using ArrayPointerSet = std::unordered_set<const TArray*, ArrayPointerHasher<TArray>, ArrayPointerEquality<TArray>>;

	/// A map of array pointers to flags.
	template<typename TArray>
	using ArrayPointerFlagMap = std::unordered_map<const TArray*, bool, ArrayPointerHasher<TArray>, ArrayPointerEquality<TArray>>;

	// region well known array sets

	/// A hash set.
	using HashSet = ArraySet<Hash256>;

	/// A key set.
	using KeySet = ArraySet<Key>;

	/// A sorted key set.
	using SortedKeySet = std::set<Key>;

	/// A hash pointer set.
	using HashPointerSet = ArrayPointerSet<Hash256>;

	/// A key pointer set.
	using KeyPointerSet = ArrayPointerSet<Key>;

	// endregion
}}

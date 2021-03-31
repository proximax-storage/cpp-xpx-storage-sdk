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

#include "model/Address.h"
#include "crypto/Hashes.h"
#include "utils/Base32.h"
#include "utils/Casting.h"
#include "exceptions.h"
#include "utils/RawBuffer.h"

namespace sirius { namespace model {

	const uint8_t Checksum_Size = 4;

	Address StringToAddress(const std::string& str) {
		if (Address_Encoded_Size != str.size())
			CATAPULT_THROW_RUNTIME_ERROR_1("encoded address has wrong size", str.size());

		return utils::Base32Decode<Address_Decoded_Size>(str);
	}

	std::string AddressToString(const Address& address) {
		return utils::Base32Encode(address);
	}

	namespace {
#ifdef SIGNATURE_SCHEME_NIS1
		constexpr auto CatapultHash = crypto::Keccak_256;
#else
		constexpr auto CatapultHash = crypto::Sha3_256;
#endif
	}

	Address PublicKeyToAddress(const Key& publicKey, NetworkIdentifier networkIdentifier) {
		// step 1: sha3 hash of the public key
		Hash256 publicKeyHash;
		CatapultHash(publicKey, publicKeyHash);

		// step 2: ripemd160 hash of (1)
		Address decoded;
		crypto::Ripemd160(publicKeyHash, reinterpret_cast<Hash160&>(decoded[1]));

		// step 3: add network identifier byte in front of (2)
		decoded[0] = utils::to_underlying_type(networkIdentifier);

		// step 4: concatenate (3) and the checksum of (3)
		Hash256 step3Hash;
		CatapultHash(utils::RawBuffer{ decoded.data(), Hash160_Size + 1 }, step3Hash);
		std::copy(step3Hash.cbegin(), step3Hash.cbegin() + Checksum_Size, decoded.begin() + Hash160_Size + 1);

		return decoded;
	}

	bool IsValidAddress(const Address& address, NetworkIdentifier networkIdentifier) {
		if (utils::to_underlying_type(networkIdentifier) != address[0])
			return false;

		Hash256 hash;
		auto checksumBegin = Address_Decoded_Size - Checksum_Size;
		CatapultHash(utils::RawBuffer{ address.data(), checksumBegin }, hash);

		return std::equal(hash.begin(), hash.begin() + Checksum_Size, address.begin() + checksumBegin);
	}

	bool IsValidEncodedAddress(const std::string& encoded, NetworkIdentifier networkIdentifier) {
		if (Address_Encoded_Size != encoded.size())
			return false;

		Address decoded;
		return utils::TryBase32Decode(encoded, decoded) && IsValidAddress(decoded, networkIdentifier);
	}
}}

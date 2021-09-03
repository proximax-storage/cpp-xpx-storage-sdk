/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once
#include "utils/ByteArray.h"
#include "utils/RawBuffer.h"
#include <string>
#include <array>
#include <functional>
#include <boost/asio/ip/tcp.hpp>


namespace sirius {

	// region byte arrays (ex address)

	constexpr size_t Signature_Size = 64;
	constexpr size_t Key_Size = 32;
	constexpr size_t Hash512_Size = 64;
	constexpr size_t Hash256_Size = 32;
	constexpr size_t Hash160_Size = 20;

	struct Signature_tag {};
	using Signature = utils::ByteArray<Signature_Size, Signature_tag>;

    struct Key_tag { static constexpr auto Byte_Size = 32; };
	using Key = utils::ByteArray<Key_Size, Key_tag>;

	struct Hash512_tag { static constexpr auto Byte_Size = 64; };
	using Hash512 = utils::ByteArray<Hash512_Size, Hash512_tag>;

	struct Hash256_tag { static constexpr auto Byte_Size = 32; };
	using Hash256 = utils::ByteArray<Hash256_Size, Hash256_tag>;

	struct Hash160_tag {};
	using Hash160 = utils::ByteArray<Hash160_Size, Hash160_tag>;

	struct GenerationHash_tag { static constexpr auto Byte_Size = 32; };
	using GenerationHash = utils::ByteArray<Hash256_Size, GenerationHash_tag>;

    struct Timestamp_tag {};
    using Timestamp = utils::BaseValue<uint64_t, Timestamp_tag>;

    constexpr size_t Address_Decoded_Size = 25;
    constexpr size_t Address_Encoded_Size = 40;

    struct Address_tag {};
    using Address = utils::ByteArray<Address_Decoded_Size, Address_tag>;

    // endregion

	template<typename T, size_t N>
	constexpr size_t CountOf(T const (&)[N]) noexcept {
		return N;
	}

    namespace drive {
        // InfoHash
        using InfoHash  = Hash256;// std::array<uint8_t,32>;

        struct ReplicatorInfo
        {
            boost::asio::ip::tcp::endpoint  m_endpoint;
            Key                             m_publicKey;
        };
        using ReplicatorList = std::vector<ReplicatorInfo>;
    }

}


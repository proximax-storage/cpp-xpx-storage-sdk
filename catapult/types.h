#pragma once
#include <array>
#include <catapult/utils/ByteArray.h>

namespace catapult {
    constexpr size_t Signature_Size = 64;
    constexpr size_t Key_Size = 32;
    constexpr size_t Hash256_Size = 32;
    constexpr size_t Hash512_Size = 64;
    constexpr size_t Hash160_Size = 20;

    struct Signature_tag {};
    using Signature = utils::ByteArray<Signature_Size, Signature_tag>;

    struct Key_tag {};
    using Key = utils::ByteArray<Key_Size, Key_tag>;

    struct Hash160_tag {};
    using Hash160 = utils::ByteArray<Hash160_Size, Hash160_tag>;

    struct Hash256_tag { static constexpr auto Byte_Size = 32; };
    using Hash256 = utils::ByteArray<Hash256_Size, Hash256_tag>;

    struct Hash512_tag { static constexpr auto Byte_Size = 64; };
    using Hash512 = utils::ByteArray<Hash512_Size, Hash512_tag>;

    struct GenerationHash_tag { static constexpr auto Byte_Size = 32; };
    using GenerationHash = utils::ByteArray<Hash256_Size, GenerationHash_tag>;

    struct Timestamp_tag {};
    using Timestamp = utils::BaseValue<uint64_t, Timestamp_tag>;
}
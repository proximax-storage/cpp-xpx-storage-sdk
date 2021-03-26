/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once
#include "utils/ByteArray.h"
#include <string>
#include <array>
#include <functional>

namespace sirius {

	// region byte arrays (ex address)

	constexpr size_t Signature_Size = 64;
	constexpr size_t Key_Size = 32;
	constexpr size_t Hash512_Size = 64;
	constexpr size_t Hash256_Size = 32;
	constexpr size_t Hash160_Size = 20;

	struct Signature_tag {};
	using Signature = utils::ByteArray<Signature_Size, Signature_tag>;

	struct Key_tag {};
	using Key = utils::ByteArray<Key_Size, Key_tag>;

	struct Hash512_tag { static constexpr auto Byte_Size = 64; };
	using Hash512 = utils::ByteArray<Hash512_Size, Hash512_tag>;

	struct Hash256_tag { static constexpr auto Byte_Size = 32; };
	using Hash256 = utils::ByteArray<Hash256_Size, Hash256_tag>;

	struct Hash160_tag {};
	using Hash160 = utils::ByteArray<Hash160_Size, Hash160_tag>;

	struct GenerationHash_tag { static constexpr auto Byte_Size = 32; };
	using GenerationHash = utils::ByteArray<Hash256_Size, GenerationHash_tag>;

	// endregion

	template<typename T, size_t N>
	constexpr size_t CountOf(T const (&)[N]) noexcept {
		return N;
	}

    // error::code
    namespace error {
        enum code {
            success = 0,
            failed  = 1
        };
    };

    // ErrorHandler
    using ErrorHandler = std::function<void(error::code code, const std::string& textMessage)>;

    // upload_status::code
    namespace upload_status {
        enum code {
            complete = 0,
            waiting_blockchain_notification = 1,
            uploading = 2,
            failed = 3
        };
    };

    // UploadHandler
    using UploadHandler = std::function<void(upload_status::code code, const std::string& info)>;

    // download_status ::code
    namespace download_status {
        enum code {
            complete = 0,
            uploading = 2,
            failed = 3
        };
    };

    // DownloadHandler
    using DownloadHandler = std::function<void(download_status::code code, const Hash256&, const std::string& info)>;
}


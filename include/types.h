/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include <string>
#include <array>
#include <functional>

//TODO!
#define DEBUG

namespace xpx_storage_sdk {

    //constexpr size_t Signature_Size = 64;
    constexpr size_t Key_Size = 32;
    //constexpr size_t Hash512_Size = 64;

	// TODO: use std::string
    constexpr size_t Hash256_Size = 64;
    //constexpr size_t Hash160_Size = 20;

    // DriveHash
    //using DriveHash = std::array<uint8_t,Hash256_Size>;

    // FileHash
    using FileHash = std::array<uint8_t,Hash256_Size>;

    // Public/Private Key
    using Key = std::array<uint8_t,Key_Size>;

    // Public/Private Key String
    using KeyString = char[Key_Size*2+1];

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
    using UploadHandler = std::function<void( upload_status::code code, const std::string& info )>;

    // download_status ::code
    namespace download_status {
        enum code {
            complete = 0,
            uploading = 2,
            failed = 3
        };
    };

    // DownloadHandler
    using DownloadHandler = std::function<void( download_status::code code, FileHash, const std::string& info )>;
}


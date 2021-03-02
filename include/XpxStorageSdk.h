/*
*** Copyright 2019 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include <string>
#include <array>

namespace xpx_storage_sdk {

    //constexpr size_t Signature_Size = 64;
    constexpr size_t Key_Size = 32;
    //constexpr size_t Hash512_Size = 64;
    constexpr size_t Hash256_Size = 32;
    //constexpr size_t Hash160_Size = 20;

    // Hash
    using DriveHash = std::array<uint8_t,Hash256_Size>;

    // Public/Private Key
    using DriveHash = std::array<uint8_t,Key_Size>;

    // error::code
    namespace error {
        enum code {
            success = 0,
            failed  = 1
        };
    };

    // ErrorHandler
    using ErrorHandler = std::function<void(error::code code, const std::string& textMessage)>;

    // error::code
    namespace upload_status {
        enum code {
            complete = 0,
            waiting_blockchain_notification = 1,
            uploading = 2,
        };
    };

    // UploadHandler
    using UploadHandler = std::function<void(upload_status::code code, const std::string& status )>;

}


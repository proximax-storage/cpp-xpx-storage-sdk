/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "log.h"

#include <string>
#include <array>
#include <functional>
#include <stdexcept>
#include <boost/asio/ip/tcp.hpp>

//TODO!
#define DEBUG

namespace sirius { namespace drive {

    using  endpoint_list = std::vector<boost::asio::ip::tcp::endpoint>;

    // InfoHash
    using InfoHash = std::array<uint8_t,32>;

    // Public/Private Key
    //using Key = std::array<uint8_t,Key_Size>;

    // Public/Private Key String
    //using KeyString = char[Key_Size*2+1];

    // Hash hex String
    //using HashHexString = char[32*2+1];

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
    using DownloadHandler = std::function<void( download_status::code code, InfoHash, std::string info )>;
}}


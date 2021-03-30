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

    // error::code
    namespace error {
        enum code {
            success = 0,
            failed  = 1
        };
    };

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
}}


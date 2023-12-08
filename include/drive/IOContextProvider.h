/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include <boost/asio/io_context.hpp>

namespace sirius::drive {

class IOContextProvider {

public:

    virtual ~IOContextProvider() = default;

    virtual boost::asio::io_context& getContext() = 0;

};

}
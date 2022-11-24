/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "WriteRPCTag.h"

#include <boost/asio.hpp>

void sirius::drive::messenger::WriteRPCTag::process( bool ok ) {

    // context is always alive
    boost::asio::post(m_context, [=, handler=std::move(m_handler)] {
        handler->onWritten(ok);
    });

}

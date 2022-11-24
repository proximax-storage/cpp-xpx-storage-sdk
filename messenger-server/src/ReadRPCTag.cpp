/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include <messenger-server/Message.h>

#include "ReadRPCTag.h"

#include <boost/asio.hpp>

namespace sirius::drive::messenger {

ReadRPCTag::ReadRPCTag( boost::asio::io_context& context, std::shared_ptr<ReadEventHandler> handler )
        : m_context( context ), m_eventHandler( std::move( handler )) {}

void ReadRPCTag::process( bool ok ) {
    if (ok) {

    }
    else {
        OutputMessage message;
        boost::asio::post(m_context, [handler=std::move(m_eventHandler)] {

        });
    }
}

}
/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include <memory>

#include "ReadEventHandler.h"

#include "RPCTag.h"

#include <boost/asio/io_context.hpp>

#include <drive/IOContextProvider.h>

#include "messengerServer.pb.h"

namespace sirius::drive::messenger
{

class ReadRPCTag
        : public RPCTag
{

public:

    messengerServer::ClientMessage m_message;

private:

    std::weak_ptr<IOContextProvider> m_context;
    std::shared_ptr<ReadEventHandler> m_eventHandler;

public:

    ReadRPCTag( std::weak_ptr<IOContextProvider> context, std::shared_ptr<ReadEventHandler> handler );

    void process( bool ok ) override;

};

}
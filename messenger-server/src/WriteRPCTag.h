/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include "RPCTag.h"

#include <boost/asio/io_context.hpp>

#include <drive/IOContextProvider.h>
#include "WriteEventHandler.h"

namespace sirius::drive::messenger
{

class WriteRPCTag
        : public RPCTag
{

private:

    std::weak_ptr<IOContextProvider> m_context;
    std::shared_ptr<WriteEventHandler> m_handler;

public:

    WriteRPCTag( std::weak_ptr<IOContextProvider> context, std::shared_ptr<WriteEventHandler> handler );

    void process( bool ok ) override;

};

}
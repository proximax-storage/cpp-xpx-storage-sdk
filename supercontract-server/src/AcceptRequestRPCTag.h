/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include "RPCTag.h"
#include "RequestContext.h"
#include <boost/asio/io_context.hpp>
#include <memory>
#include <drive/IOContextProvider.h>

namespace sirius::drive::contract
{

class AcceptRequestRPCTag
        : public RPCTag
{

private:

    std::shared_ptr<RequestContext> m_requestContext;
    std::function<void()> m_addNewAcceptRequestTag;
    std::weak_ptr<IOContextProvider> m_ioContext;

public:

    AcceptRequestRPCTag( std::shared_ptr<RequestContext> requestContext,
                         std::function<void()> addNewAcceptRequestTag,
                         std::weak_ptr<IOContextProvider> ioContext );

    void process( bool ok ) override;

};

}
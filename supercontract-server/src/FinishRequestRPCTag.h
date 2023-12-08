/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include <memory>
#include "RequestContext.h"
#include "RPCTag.h"

namespace sirius::drive::contract
{

class FinishRequestRPCTag: public RPCTag
{

private:

    std::shared_ptr<RequestContext> m_requestContext;

public:

    FinishRequestRPCTag(std::shared_ptr<RequestContext> requestContext);

    void process( bool ok ) override;

};

}
/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "FinishRequestRPCTag.h"

namespace sirius::drive::contract
{

FinishRequestRPCTag::FinishRequestRPCTag( std::shared_ptr<RequestContext> requestContext )
        : m_requestContext( std::move( requestContext ))
{}

void FinishRequestRPCTag::process( bool ok )
{

}


}

/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "FinishRPCTag.h"

namespace sirius::drive::messenger {

FinishRPCTag::FinishRPCTag( std::shared_ptr<RPCContext> context )
        : m_context( std::move( context )) {}

void FinishRPCTag::process( bool ok ) {

}

}
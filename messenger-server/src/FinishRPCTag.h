/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "RPCTag.h"
#include "StreamContext.h"

namespace sirius::drive::messenger
{

class FinishRPCTag
        : public RPCTag
{

private:

    std::shared_ptr<StreamContext> m_context;

public:

    FinishRPCTag( std::shared_ptr<StreamContext> context );

    void process( bool ok ) override;

};

}
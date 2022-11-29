/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

namespace sirius::drive::messenger
{

class RPCTag
{

public:

    virtual ~RPCTag() = default;

    virtual void process( bool ok ) = 0;

};

}
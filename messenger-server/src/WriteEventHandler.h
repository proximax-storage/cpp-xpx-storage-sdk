/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

namespace sirius::drive::messenger
{

class WriteEventHandler {

public:

    virtual ~WriteEventHandler() = default;

    virtual void onWritten(bool ok) = 0;

};

}
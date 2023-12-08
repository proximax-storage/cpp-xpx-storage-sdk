/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

namespace sirius::drive {

class SupercontractRequest {

public:

    virtual ~SupercontractRequest() = default;

    virtual uint64_t getId() = 0;

};

}

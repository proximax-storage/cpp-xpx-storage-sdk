/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include <types.h>

namespace sirius::drive::messenger {

struct InputMessage {
    std::string m_tag;
    std::string m_data;
};

struct OutputMessage {
    Key m_receiver;
    std::string m_tag;
    std::string m_data;
};

}
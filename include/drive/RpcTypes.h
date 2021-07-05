/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "types.h"
#include "rpc/msgpack/adaptor/define_decl.hpp"
#include "rpc/msgpack.hpp"


namespace sirius { namespace drive {

        struct ResultWithInfoHash {
            std::array<uint8_t,32>  m_rootHash;
            std::string             m_error;
            MSGPACK_DEFINE_ARRAY(m_rootHash,m_error);
        };

        struct ResultWithModifyStatus {
            int                     m_modifyStatus;
            std::string             m_error;
            MSGPACK_DEFINE_ARRAY(m_modifyStatus,m_error);
        };
}}

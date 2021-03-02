/*
*** Copyright 2019 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include <string>
#include <vector>

namespace xpx_storage_sdk {

    // action_list_id::code
    namespace action_list_id {
        enum code
        {
            UPLOAD      = 1,
            NEW_FOLDER  = 2,
            RENAME      = 3,
            REMOVE      = 4,
        };
    };

    // Action
    struct Action {
        Action() = default;

        action_list_id::code m_actionId;
        std::string          m_param1;
        std::string          m_param2;
    };

    // ActionList
    struct ActionList : public std::vector<Action>
    {
        void serialize( std::string fileName );
        void deserialize( std::string fileName );
    };

};

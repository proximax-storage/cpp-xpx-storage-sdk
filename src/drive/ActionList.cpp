/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "drive/ActionList.h"
#include "drive/log.h"

#include <fstream>

#include <cereal/types/vector.hpp>
#include <cereal/types/array.hpp>
#include <cereal/types/memory.hpp>
//#include <cereal/archives/binary.hpp>
#include <cereal/archives/portable_binary.hpp>

namespace sirius { namespace drive {

void ActionList::serialize( const std::filesystem::path& fileName ) const
{
    std::ofstream os( fileName, std::ios::binary | std::ios::trunc );
    cereal::PortableBinaryOutputArchive archive( os );
    archive( *this );
}

void ActionList::deserialize( const std::filesystem::path& fileName )
{
    clear();
    std::ifstream is( fileName, std::ios::binary );
    cereal::PortableBinaryInputArchive iarchive(is);
    iarchive( *this );
}

void ActionList::dbgPrint() const
{
    __LOG("ActionList {")
    for( const auto& action : *this )
    {
        switch( action.m_actionId )
        {
            case action_list_id::upload:
                __LOG(" upload: '" << action.m_param1 << "' to '" << action.m_param2 << "'")
                break;
            case action_list_id::new_folder:
                __LOG(" new_folder: '" << action.m_param1 << "'")
                break;
            case action_list_id::move:
                __LOG(" move: '" << action.m_param1 << "' to '" << action.m_param2 << "'")
                break;
            case action_list_id::remove:
                __LOG(" remove: '" << action.m_param1 << "'")
                break;
        }
    }
    __LOG("}")
}

}}

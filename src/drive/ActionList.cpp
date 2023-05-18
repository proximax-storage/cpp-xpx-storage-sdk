/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "drive/ActionList.h"

#include <fstream>

#include <cereal/types/vector.hpp>
#include <cereal/types/array.hpp>
#include <cereal/types/memory.hpp>
//#include <cereal/archives/binary.hpp>
#include <cereal/archives/portable_binary.hpp>

namespace sirius { namespace drive {

void ActionList::serialize( std::string fileName ) const
{
    std::ofstream os( fileName, std::ios::binary | std::ios::trunc );
    cereal::PortableBinaryOutputArchive archive( os );
    archive( *this );
}

void ActionList::deserialize( std::string fileName )
{
    clear();
    std::ifstream is( fileName, std::ios::binary );
    cereal::PortableBinaryInputArchive iarchive(is);
    iarchive( *this );
}

void ActionList::dbgPrint() const
{
    std::cerr << "ActionList {" << std::endl;
    for( const auto& action : *this )
    {
        switch( action.m_actionId )
        {
            case action_list_id::upload:
                std::cerr << " upload: '" << action.m_param1 << "' to '" << action.m_param2 << "'" << std::endl;
                break;
            case action_list_id::new_folder:
                std::cerr << " new_folder: '" << action.m_param1 << "'" << std::endl;
                break;
            case action_list_id::move:
                std::cerr << " move: '" << action.m_param1 << "' to '" << action.m_param2 << "'" << std::endl;
                break;
            case action_list_id::remove:
                std::cerr << " remove: '" << action.m_param1 << "'" << std::endl;
                break;
        }
    }
    std::cerr << "}" << std::endl;
}

}}

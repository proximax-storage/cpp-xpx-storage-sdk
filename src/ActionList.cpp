/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "ActionList.h"

#include <fstream>

#include <cereal/types/vector.hpp>
#include <cereal/types/array.hpp>
#include <cereal/types/memory.hpp>
//#include <cereal/archives/binary.hpp>
#include <cereal/archives/portable_binary.hpp>

namespace sirius { namespace drive {

void ActionList::serialize( std::string fileName ) const
{
    std::ofstream os( fileName, std::ios::binary );
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

}}

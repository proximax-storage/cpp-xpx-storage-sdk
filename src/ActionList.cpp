/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "ActionList.h"

#include <cereal/types/vector.hpp>
#include <cereal/types/memory.hpp>
#include <cereal/archives/binary.hpp>

using namespace xpx_storage_sdk;

void ActionList::serialize( std::string fileName )
{
    std::ofstream os( fileName, std::ios::binary );
    cereal::BinaryOutputArchive archive( os );
//            archive( static_cast<uint16_t>( size() ));
//            for( uint i=0; i<size(); i++ ) {
//                archive( at(i) );
//            }
    archive( *this );
}

void ActionList::deserialize( std::string fileName )
{
    std::ifstream is( fileName, std::ios::binary );
    cereal::BinaryInputArchive iarchive(is);

//            uint16_t size;
//            iarchive( size );
//            for( uint i=0; i<size; i++ ) {
//                push_back( Action() );
//                iarchive( at(i) );
//            }
    iarchive( *this );
}

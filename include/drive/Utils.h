/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "types.h"
#include <filesystem>
#include "plugins.h"
#include <boost/utility/string_view.hpp>

namespace sirius { namespace drive {

namespace fs = std::filesystem;

// magnetLink
PLUGIN_API std::string magnetLink( const InfoHash& key );

// toString
PLUGIN_API std::string toString( const InfoHash& key );
PLUGIN_API std::string arrayToString( const Key& key );

PLUGIN_API std::string toString( const std::array<uint8_t,32>& key );

// hashToFileName
PLUGIN_API std::string hashToFileName( const InfoHash& key );

PLUGIN_API bool isPathInsideFolder( const fs::path& path, const fs::path& folder );

PLUGIN_API Hash256 stringToHash( const boost::string_view& str );
PLUGIN_API std::string hexToString( const void* begin, const void* end );

PLUGIN_API Hash256 ltDataToHash( const char* ptr );

template<class T>
PLUGIN_API T randomByteArray()
{
    T data;
    for (auto it = data.begin(); it != data.end(); it++)
    {
        *it = static_cast<uint8_t>(rand() % 256);
    }
    return data;
}

}}


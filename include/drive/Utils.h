/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "types.h"
#include "log.h"
#include <filesystem>
#include "plugins.h"
#include <boost/utility/string_view.hpp>

namespace sirius { namespace drive {

namespace fs = std::filesystem;

// magnetLink
PLUGIN_API std::string magnetLink( const InfoHash& key );

PLUGIN_API fs::path toPath( const std::string& s );

// toString
PLUGIN_API std::string toString( const InfoHash& key );
PLUGIN_API std::string arrayToString( const Key& key );

PLUGIN_API std::string toString( const std::array<uint8_t,32>& key );

// hashToFileName
PLUGIN_API std::string hashToFileName( const InfoHash& key );

PLUGIN_API bool isPathInsideFolder( const fs::path& path, const fs::path& folder, std::error_code& ec );

PLUGIN_API int charToInt( char input );

template<class T>
PLUGIN_API T stringToByteArray( const boost::string_view& str ) {
    if ( str.size() != 64 )
        throw std::invalid_argument("Invalid input string");

    T t;

    for( unsigned int i=0; i<32; i++ )
    {
        t[i] = (charToInt(str[2*i])<<4) + charToInt(str[2*i+1]);
    }

    return t;
};
PLUGIN_API std::string hexToString( const void* begin, const void* end );

PLUGIN_API Hash256 ltDataToHash( const char* ptr );

template<class T>
T randomByteArray()
{
    T data;
    for (auto it = data.begin(); it != data.end(); it++)
    {
        *it = static_cast<uint8_t>(rand() % 256);
    }
    return data;
}

inline void moveFile( const fs::path& src, const fs::path& dest )
{
    std::error_code ec;
    fs::rename( src, dest, ec );
    if ( ec )
    {
        __LOG( "WARNING!!!: Cannot move " << src.string() << " to " << dest.string() << " : " << ec.message() );
        fs::copy( src, dest );
        fs::remove( src );
    }
}
}}


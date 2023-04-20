/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "drive/Utils.h"

namespace sirius { namespace drive {

static char byteMap[256][2] = {
    {'0','0'}, {'0','1'}, {'0','2'}, {'0','3'}, {'0','4'}, {'0','5'}, {'0','6'}, {'0','7'}, {'0','8'}, {'0','9'}, {'0','a'}, {'0','b'}, {'0','c'}, {'0','d'}, {'0','e'}, {'0','f'},
    {'1','0'}, {'1','1'}, {'1','2'}, {'1','3'}, {'1','4'}, {'1','5'}, {'1','6'}, {'1','7'}, {'1','8'}, {'1','9'}, {'1','a'}, {'1','b'}, {'1','c'}, {'1','d'}, {'1','e'}, {'1','f'},
    {'2','0'}, {'2','1'}, {'2','2'}, {'2','3'}, {'2','4'}, {'2','5'}, {'2','6'}, {'2','7'}, {'2','8'}, {'2','9'}, {'2','a'}, {'2','b'}, {'2','c'}, {'2','d'}, {'2','e'}, {'2','f'},
    {'3','0'}, {'3','1'}, {'3','2'}, {'3','3'}, {'3','4'}, {'3','5'}, {'3','6'}, {'3','7'}, {'3','8'}, {'3','9'}, {'3','a'}, {'3','b'}, {'3','c'}, {'3','d'}, {'3','e'}, {'3','f'},
    {'4','0'}, {'4','1'}, {'4','2'}, {'4','3'}, {'4','4'}, {'4','5'}, {'4','6'}, {'4','7'}, {'4','8'}, {'4','9'}, {'4','a'}, {'4','b'}, {'4','c'}, {'4','d'}, {'4','e'}, {'4','f'},
    {'5','0'}, {'5','1'}, {'5','2'}, {'5','3'}, {'5','4'}, {'5','5'}, {'5','6'}, {'5','7'}, {'5','8'}, {'5','9'}, {'5','a'}, {'5','b'}, {'5','c'}, {'5','d'}, {'5','e'}, {'5','f'},
    {'6','0'}, {'6','1'}, {'6','2'}, {'6','3'}, {'6','4'}, {'6','5'}, {'6','6'}, {'6','7'}, {'6','8'}, {'6','9'}, {'6','a'}, {'6','b'}, {'6','c'}, {'6','d'}, {'6','e'}, {'6','f'},
    {'7','0'}, {'7','1'}, {'7','2'}, {'7','3'}, {'7','4'}, {'7','5'}, {'7','6'}, {'7','7'}, {'7','8'}, {'7','9'}, {'7','a'}, {'7','b'}, {'7','c'}, {'7','d'}, {'7','e'}, {'7','f'},
    {'8','0'}, {'8','1'}, {'8','2'}, {'8','3'}, {'8','4'}, {'8','5'}, {'8','6'}, {'8','7'}, {'8','8'}, {'8','9'}, {'8','a'}, {'8','b'}, {'8','c'}, {'8','d'}, {'8','e'}, {'8','f'},
    {'9','0'}, {'9','1'}, {'9','2'}, {'9','3'}, {'9','4'}, {'9','5'}, {'9','6'}, {'9','7'}, {'9','8'}, {'9','9'}, {'9','a'}, {'9','b'}, {'9','c'}, {'9','d'}, {'9','e'}, {'9','f'},
    {'a','0'}, {'a','1'}, {'a','2'}, {'a','3'}, {'a','4'}, {'a','5'}, {'a','6'}, {'a','7'}, {'a','8'}, {'a','9'}, {'a','a'}, {'a','b'}, {'a','c'}, {'a','d'}, {'a','e'}, {'a','f'},
    {'b','0'}, {'b','1'}, {'b','2'}, {'b','3'}, {'b','4'}, {'b','5'}, {'b','6'}, {'b','7'}, {'b','8'}, {'b','9'}, {'b','a'}, {'b','b'}, {'b','c'}, {'b','d'}, {'b','e'}, {'b','f'},
    {'c','0'}, {'c','1'}, {'c','2'}, {'c','3'}, {'c','4'}, {'c','5'}, {'c','6'}, {'c','7'}, {'c','8'}, {'c','9'}, {'c','a'}, {'c','b'}, {'c','c'}, {'c','d'}, {'c','e'}, {'c','f'},
    {'d','0'}, {'d','1'}, {'d','2'}, {'d','3'}, {'d','4'}, {'d','5'}, {'d','6'}, {'d','7'}, {'d','8'}, {'d','9'}, {'d','a'}, {'d','b'}, {'d','c'}, {'d','d'}, {'d','e'}, {'d','f'},
    {'e','0'}, {'e','1'}, {'e','2'}, {'e','3'}, {'e','4'}, {'e','5'}, {'e','6'}, {'e','7'}, {'e','8'}, {'e','9'}, {'e','a'}, {'e','b'}, {'e','c'}, {'e','d'}, {'e','e'}, {'e','f'},
    {'f','0'}, {'f','1'}, {'f','2'}, {'f','3'}, {'f','4'}, {'f','5'}, {'f','6'}, {'f','7'}, {'f','8'}, {'f','9'}, {'f','a'}, {'f','b'}, {'f','c'}, {'f','d'}, {'f','e'}, {'f','f'},
};

std::string magnetLink( const InfoHash& key ) {

    char hashStr[64+1];

    for( uint32_t i=0; i<32; i++ ) {
        hashStr[2*i]   = byteMap[key[i]][0];
        hashStr[2*i+1] = byteMap[key[i]][1];
    }
    hashStr[64] = 0;

    return std::string("magnet:?xt=urn:btmh:1220") + hashStr;
}

PLUGIN_API fs::path toPath( const std::string& s ) {
    return fs::path(s.begin(), s.end());
}

std::string toString( const InfoHash& key ) {

    char hashStr[64];

    for( uint32_t i=0; i<32; i++ ) {
        hashStr[2*i]   = byteMap[key[i]][0];
        hashStr[2*i+1] = byteMap[key[i]][1];
    }

    return std::string( hashStr, hashStr+64 );
}

std::string toString( const std::array<uint8_t,32>& key ) {

    char hashStr[64];

    for( uint32_t i=0; i<32; i++ ) {
        hashStr[2*i]   = byteMap[key[i]][0];
        hashStr[2*i+1] = byteMap[key[i]][1];
    }

    return std::string( hashStr, hashStr+64 );
}

std::string arrayToString( const Key& key ) {

    char hashStr[64];

    for( uint32_t i=0; i<32; i++ ) {
        hashStr[2*i]   = byteMap[key[i]][0];
        hashStr[2*i+1] = byteMap[key[i]][1];
    }

    return std::string( hashStr, hashStr+64 );
}

std::string hashToFileName( const InfoHash& key ) {

    char hashStr[64];

    for( uint32_t i=0; i<32; i++ ) {
        hashStr[2*i]   = byteMap[key[i]][0];
        hashStr[2*i+1] = byteMap[key[i]][1];
    }

    return std::string( hashStr, hashStr+64 );
}


bool isPathInsideFolder( const fs::path& path, const fs::path& folder )
{
    fs::path path0 = fs::absolute(path);
    fs::path folder0 = fs::absolute(folder);

    // if root paths are different, return false
    if( path0.root_path() != folder0.root_path() )
        return false;

    // find out where the two paths diverge
    fs::path::const_iterator pIt = path0.begin();
    fs::path::const_iterator fIt = folder0.begin();
    while( fIt != folder0.end()  )
    {
        if ( pIt == path0.end() )
            return true;

        if ( *pIt != *fIt )
            return false;

        pIt++;
        fIt++;
    }

    return false;
}

int charToInt( char input )
{
    if(input >= '0' && input <= '9')
        return input - '0';

    if(input >= 'A' && input <= 'F')
        return input - 'A' + 10;

    if(input >= 'a' && input <= 'f')
        return input - 'a' + 10;

    throw std::invalid_argument("Invalid input string");
}

std::string hexToString( const void* begin, const void* end )
{
    uint8_t* ptr = (uint8_t*)begin;
    uint8_t* ptrEnd = (uint8_t*)end;
    assert( ptr < ptrEnd );
    assert( ptrEnd-ptr < 10*1024 );
    std::string str;
    str.reserve( 2*(ptrEnd-ptr) );

    for( ; ptr<ptrEnd; ptr++ ) {
        str.append( 1, byteMap[*ptr][0] );
        str.append( 1, byteMap[*ptr][1] );
    }

    return str;
}

PLUGIN_API Hash256 ltDataToHash( const char* ptr )
{
    Hash256 hash;
    memcpy( &hash[0], ptr, 32 );
    return hash;
}

}}

/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "types.h"
#include <filesystem>

namespace xpx_storage_sdk {

namespace fs = std::filesystem;

// magnetLink
std::string magnetLink( const InfoHash& key );

// toString
std::string toString( const InfoHash& key );

bool isPathInsideFolder( fs::path path, fs::path folder );

}


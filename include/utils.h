/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "types.h"
#include <filesystem>

namespace sirius { namespace drive {

namespace fs = std::filesystem;

// magnetLink
std::string magnetLink( const InfoHash& key );

// toString
std::string toString( const InfoHash& key );

bool isPathInsideFolder( const fs::path& path, const fs::path& folder );

}}


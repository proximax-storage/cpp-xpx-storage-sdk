/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once
#include "types.h"

namespace sirius { namespace drive {

	template<typename TArray>
	std::string ToString(const TArray& array);

	std::string CreateMagnetLink(const Hash256& hash);
}}


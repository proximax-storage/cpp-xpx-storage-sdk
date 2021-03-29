/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#include <sstream>
#include "drive/Utils.h"

namespace sirius { namespace drive {

	template<typename TArray>
	std::string ToString(const TArray& array) {
		std::ostringstream stream;
		stream << array;
		return stream.str();
	}

	std::string CreateMagnetLink(const Hash256& hash) {
		return std::string("magnet:?xt=urn:btmh:1220") + ToString(hash);
	}

}}

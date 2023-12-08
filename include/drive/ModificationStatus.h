/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

namespace sirius::drive {
	enum class ModificationStatus : uint8_t {
		SUCCESS,
		INVALID_ACTION_LIST,
		ACTION_LIST_IS_ABSENT,
		NOT_ENOUGH_SPACE,
		DOWNLOAD_FAILED
	};
}
/*#include <memory>
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "ActionList.h"

namespace xpx_storage_sdk {

    FileHash prepareActionListToUpload( const ActionList&, const std::string& tmpFolderPath );
};

/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include <iostream>
#include <mutex>

inline std::mutex gLogMutex;

#define LOG(expr)

#define __LOG(expr) { \
        const std::lock_guard<std::mutex> autolock( gLogMutex ); \
        std::cerr << expr << std::endl << std::flush; \
    }

#define _LOG(expr) { \
        const std::lock_guard<std::mutex> autolock( gLogMutex ); \
        std::cerr << m_dbgOurPeerName << ": " << expr << std::endl << std::flush; \
    }

/*#define LOG_WARN(expr) { \
    const std::lock_guard<std::mutex> autolock( gLogMutex ); \
    std::cerr << __FILE__ << ":" << __LINE__ << ": "<< expr << std::flush; \
    std::cerr << expr << std::flush; \
}*/

#define LOG_ERR(expr) { \
    const std::lock_guard<std::mutex> autolock( gLogMutex ); \
    std::cerr << __FILE__ << ":" << __LINE__ << ": "<< expr << std::flush; \
}

#define _ASSERT(expr) { \
    if (!(expr)) {\
        const std::lock_guard<std::mutex> autolock( gLogMutex ); \
        std::cerr << __FILE__ << ":" << __LINE__ << " failed: " << #expr << std::flush; \
        assert(0); \
    }\
}

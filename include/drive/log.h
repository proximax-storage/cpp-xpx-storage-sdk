/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include <iostream>
#include <mutex>

inline std::mutex gLogMutex;

//todo
#ifndef DEBUG_OFF_CATAPULT
#define DEBUG_OFF_CATAPULT
#endif

#define LOG(expr)

// __LOG
#ifdef DEBUG_OFF_CATAPULT
    #define __LOG(expr) { \
            const std::lock_guard<std::mutex> autolock( gLogMutex ); \
            std::cout << expr << std::endl << std::flush; \
        }
#else
    #define __LOG(expr) { \
            std::ostringstream out; \
            out << m_dbgOurPeerName << ": " << expr; \
            CATAPULT_LOG(debug) << out.str(); \
    }
#endif

// _LOG
#ifdef DEBUG_OFF_CATAPULT
    #define _LOG(expr) { \
            const std::lock_guard<std::mutex> autolock( gLogMutex ); \
            std::cout << m_dbgOurPeerName << ": " << expr << std::endl << std::flush; \
        }
#else
    #define _LOG(expr) { \
            std::ostringstream out; \
            out << m_dbgOurPeerName << ": " << expr; \
            CATAPULT_LOG(debug) << out.str(); \
        }
#endif

inline bool gBreakOnWarning = false;

// LOG_WARN
#ifdef DEBUG_OFF_CATAPULT
    #define _LOG_WARN(expr) { \
            const std::lock_guard<std::mutex> autolock( gLogMutex ); \
            std::cout << m_dbgOurPeerName << ": WARNING!!! in " << __FUNCTION__ << "() " << expr << std::endl << std::flush; \
            if ( gBreakOnWarning ) { assert(0); } \
        }
#else
    #define _LOG_WARN(expr) { \
            std::ostringstream out; \
            cout << m_dbgOurPeerName << ": WARNING!!! in " << __FUNCTION__ << "() " << expr; \
            CATAPULT_LOG(debug) << out.str(); \
            if ( gBreakOnWarning ) { assert(0); } \
        }
#endif

#ifdef DEBUG_OFF_CATAPULT
    #define __LOG_WARN(expr) { \
            const std::lock_guard<std::mutex> autolock( gLogMutex ); \
            std::cout << ": WARNING!!! in " << __FUNCTION__ << "() " << expr << std::endl << std::flush; \
            if ( gBreakOnWarning ) { assert(0); } \
        }
#else
    #define __LOG_WARN(expr) { \
            std::ostringstream out; \
            cout << ": WARNING!!! in " << __FUNCTION__ << "() " << expr; \
            CATAPULT_LOG(debug) << out.str(); \
            if ( gBreakOnWarning ) { assert(0); } \
        }
#endif

inline bool gBreakOnError = true;

// LOG_ERR
#ifdef DEBUG_OFF_CATAPULT
    #define _LOG_ERR(expr) { \
        const std::lock_guard<std::mutex> autolock( gLogMutex ); \
        std::cout << __FILE__ << ":" << __LINE__ << ": " << __FUNCTION__ << ": "<< expr << "\n" << std::flush; \
        if ( gBreakOnError ) { assert(0); } \
    }
#else
    #define _LOG_ERR(expr) { \
        const std::lock_guard<std::mutex> autolock( gLogMutex ); \
        std::ostringstream out; \
        out << "ERROR!!! " << __FILE__ << ":" << __LINE__ << ": " << __FUNCTION__ << ": "<< expr; \
        CATAPULT_LOG(error) << out.str(); \
    }
#endif

#if 1
#define _FUNC_ENTRY();
#else
#define _FUNC_ENTRY() { \
        const std::lock_guard<std::mutex> autolock( gLogMutex ); \
        std::cout << m_dbgOurPeerName << ": call: " << __PRETTY_FUNCTION__ << std::endl << std::flush; \
    }
#endif

#define _ASSERT(expr) { \
    if (!(expr)) {\
        const std::lock_guard<std::mutex> autolock( gLogMutex ); \
        if (0) \
            std::cerr << m_dbgOurPeerName << ": " << __FILE__ << ":" << __LINE__ << " failed: " << #expr << "\n" << std::flush; \
        else \
            std::cerr << m_dbgOurPeerName << ": failed assert: " << #expr << "\n" << std::flush; \
        assert(0); \
    }\
}

#define __ASSERT(expr) { \
    if (!(expr)) {\
        const std::lock_guard<std::mutex> autolock( gLogMutex ); \
            std::cerr << __FILE__ << ":" << __LINE__ << " failed: " << #expr << "\n" << std::flush; \
            std::cerr << "failed assert!!!: " << #expr << "\n" << std::flush; \
        assert(0); \
    }\
}

/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include <iostream>
#include <mutex>

#include "boost/date_time/posix_time/posix_time.hpp"

inline std::mutex gLogMutex;

inline std::string current_time()
{
    // Get current time from the clock, using microseconds resolution
    const boost::posix_time::ptime now =
            boost::posix_time::microsec_clock::local_time();

    // Get the time offset in current day
    const boost::posix_time::time_duration td = now.time_of_day();
    const auto date = now.date();
    //
    // Extract hours, minutes, seconds and milliseconds.
    //
    // Since there is no direct accessor ".milliseconds()",
    // milliseconds are computed _by difference_ between total milliseconds
    // (for which there is an accessor), and the hours/minutes/seconds
    // values previously fetched.
    //
    const long day          = date.day().as_number();
    const long month        = date.month().as_number();
    const long year         = date.year();
    const long hours        = td.hours();
    const long minutes      = td.minutes();
    const long seconds      = td.seconds();
    const long milliseconds = td.total_milliseconds() -
            ((hours * 3600 + minutes * 60 + seconds) * 1000);

    //
    // Format like this:
    //
    //      hh:mm:ss.SS
    //
    // e.g. 02:15:40:321
    //
    //      ^          ^
    //      |          |
    //      123456789*12
    //      ---------10-     --> 12 chars + \0 --> 13 chars should suffice
    //
    //
    char buf[40];
    sprintf(buf, "%04ld.%02ld.%02ld %02ld:%02ld:%02ld.%03ld",
            year, month, day, hours, minutes, seconds, milliseconds);

    return buf;
}

//todo
#ifndef DEBUG_OFF_CATAPULT
#define DEBUG_OFF_CATAPULT
#endif

#define LOG(expr)

// __LOG
#ifdef DEBUG_OFF_CATAPULT
    #define __LOG(expr) { \
            const std::lock_guard<std::mutex> autolock( gLogMutex ); \
            std::cout << current_time() << "\t" << expr << std::endl << std::flush; \
        }
#else
    #define __LOG(expr) { \
            const std::lock_guard<std::mutex> autolock( gLogMutex ); \
            std::ostringstream out; \
            out << current_time() << "\t" << expr; \
            CATAPULT_LOG(debug) << out.str(); \
    }
#endif

// _LOG
#ifdef DEBUG_OFF_CATAPULT
    #define _LOG(expr) { \
            const std::lock_guard<std::mutex> autolock( gLogMutex ); \
            std::cout << current_time() << "\t" << m_dbgOurPeerName << ": " << expr << std::endl << std::flush; \
        }
#else
    #define _LOG(expr) { \
            std::ostringstream out; \
            out << current_time() << "\t" << m_dbgOurPeerName << ": " << expr; \
            CATAPULT_LOG(debug) << out.str(); \
        }
#endif

inline bool gBreakOnWarning = false;

inline bool gIsRemoteRpcClient = false;

//inline void checkLogFileSize()
//{
//    if ( !gIsRemoteRpcClient )
//        return;
//        
//    auto pos = lseek( 0, 0, SEEK_CUR );
//    if ( pos > 100*1024*1024 )
//    {
//        close(0);
//        close(1);
//        std::filesystem
//    }
//}

// LOG_WARN
#ifdef DEBUG_OFF_CATAPULT
    #define _LOG_WARN(expr) { \
            const std::lock_guard<std::mutex> autolock( gLogMutex ); \
            std::cout << current_time() << "\t" << m_dbgOurPeerName << ": WARNING!!! in " << __FUNCTION__ << "() " << expr << std::endl << std::flush; \
            if ( gBreakOnWarning ) { assert(0); } \
        }
#else
    #define _LOG_WARN(expr) { \
            std::ostringstream out; \
            out << current_time() << "\t" << m_dbgOurPeerName << ": WARNING!!! in " << __FUNCTION__ << "() " << expr; \
            CATAPULT_LOG(debug) << out.str(); \
            if ( gBreakOnWarning ) { assert(0); } \
        }
#endif

#ifdef DEBUG_OFF_CATAPULT
    #define __LOG_WARN(expr) { \
            const std::lock_guard<std::mutex> autolock( gLogMutex ); \
            std::cout << ": WARNING!!! in " << __FUNCTION__ << "() " << current_time() << "\t" << expr << std::endl << std::flush; \
            if ( gBreakOnWarning ) { assert(0); } \
        }
#else
    #define __LOG_WARN(expr) { \
            std::ostringstream out; \
            cout << ": WARNING!!! in " << __FUNCTION__ << "() " << current_time() << "\t" << expr; \
            CATAPULT_LOG(debug) << out.str(); \
            if ( gBreakOnWarning ) { assert(0); } \
        }
#endif

inline bool gBreakOnError = true;

// LOG_ERR
#ifdef DEBUG_OFF_CATAPULT
    #define _LOG_ERR(expr) { \
        const std::lock_guard<std::mutex> autolock( gLogMutex ); \
        std::cout << __FILE__ << ":" << __LINE__ << ": " << __FUNCTION__ << ": "<< current_time() << "\t" << expr << "\n" << std::flush; \
        if ( gBreakOnError ) { assert(0); } \
    }
#else
    #define _LOG_ERR(expr) { \
        const std::lock_guard<std::mutex> autolock( gLogMutex ); \
        std::ostringstream out; \
        out << "ERROR!!! " << __FILE__ << ":" << __LINE__ << ": " << __FUNCTION__ << ": "<< current_time() << "\t" << expr; \
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
        std::cerr << m_dbgOurPeerName << ": " << __FILE__ << ":" << __LINE__ << " failed: " << current_time() << "\t" << #expr << "\n" << std::flush; \
        else \
        std::cerr << m_dbgOurPeerName << ": failed assert: " << current_time() << "\t" << #expr << "\n" << std::flush; \
        assert(0); \
    }\
}

#define __ASSERT(expr) { \
    if (!(expr)) {\
        const std::lock_guard<std::mutex> autolock( gLogMutex ); \
        std::cerr << __FILE__ << ":" << __LINE__ << " failed: " << current_time() << "\t" << #expr << "\n" << std::flush; \
        std::cerr << "failed assert!!!: " << current_time() << "\t" << #expr << "\n" << std::flush; \
        assert(0); \
    }\
}

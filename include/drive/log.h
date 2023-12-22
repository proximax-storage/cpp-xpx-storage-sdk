/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#if (_MSC_VER)
#pragma warning (disable : 4244)
#pragma warning (disable : 4267)
#pragma warning (disable : 5054)

#include <io.h>
#endif

#include <iostream>
#include <mutex>
#include <filesystem>
#include <functions.h>
#include <optional>

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
    std::snprintf(buf, sizeof(buf), "%04ld.%02ld.%02ld %02ld:%02ld:%02ld.%03ld",
                  year, month, day, hours, minutes, seconds, milliseconds);

    return buf;
}

inline bool gBreakOnWarning = false;
inline bool gBreakOnError   = true;


inline bool gIsRemoteRpcClient = false;
inline std::optional<std::function<void()>> gCreateLogBackup = {};

inline void checkLogFileSize()
{
    if ( !gIsRemoteRpcClient )
        return;

#if _MSC_VER
    auto pos = _lseek( 1, 0, SEEK_END );
#else
    auto pos = lseek( 1, 0, SEEK_END );
#endif

    if ( ( pos > 100 * 1024 * 1024 ) && gCreateLogBackup )
    {
        (*gCreateLogBackup)();
    }
}

// LOG
#define LOG(expr) {}
/*
#define LOG(expr) { \
        std::lock_guard<std::mutex> autolock( gLogMutex ); \
        checkLogFileSize(); \
        std::cout << current_time() << " " << expr << std::endl << std::flush; \
    }
*/


// _LOG - with m_dbgOurPeerName
//#define _LOG(expr) {}

#define _LOG(expr) { \
        std::lock_guard<std::mutex> autolock( gLogMutex ); \
        checkLogFileSize(); \
        std::cout << current_time() << " " << m_dbgOurPeerName << ": " << expr << std::endl << std::flush; \
    }

// __LOG
//#define __LOG(expr) {}

#define __LOG(expr) { \
        std::lock_guard<std::mutex> autolock( gLogMutex ); \
        checkLogFileSize(); \
        std::cout << current_time() << " " << expr << std::endl << std::flush; \
    }


// ___LOG
//#define ___LOG(expr) {}

 #define ___LOG(expr) { \
        std::lock_guard<std::mutex> autolock( gLogMutex ); \
        checkLogFileSize(); \
        std::cout << current_time() << " " << expr << std::endl << std::flush; \
    }


// _LOG_WARN - with m_dbgOurPeerName
#define _LOG_WARN(expr) { \
        std::lock_guard<std::mutex> autolock( gLogMutex ); \
        checkLogFileSize(); \
        std::cout << current_time() << " " << m_dbgOurPeerName << ": WARNING!!! in " << __FUNCTION__ << "() " << expr << std::endl << std::flush; \
        if ( gBreakOnWarning ) { assert(0); } \
    }

#define __LOG_WARN(expr) { \
        std::lock_guard<std::mutex> autolock( gLogMutex ); \
        checkLogFileSize(); \
        std::cout << ": WARNING!!! in " << __FUNCTION__ << "() " << current_time() << " " << expr << std::endl << std::flush; \
        if ( gBreakOnWarning ) { assert(0); } \
    }

#define _LOG_ERR(expr) { \
        std::lock_guard<std::mutex> autolock( gLogMutex ); \
        checkLogFileSize(); \
        std::cout << __FILE__ << ":" << __LINE__ << ": " << __FUNCTION__ << ": "<< current_time() << " " << expr << "\n" << std::flush; \
        if ( gBreakOnError ) { assert(0); } \
    }

#if 0
#define _FUNC_ENTRY
#else
inline int gCallLevel = 0;
inline char gIndentString[] = "          " "          " "          " "          " "          " "          " "          " "          " "          " "          ";
struct FuncEntry
{
    const std::string m_PRETTY_FUNCTION;
    const std::string m_dbgOurPeerName;
    
    FuncEntry( const std::string& PRETTY_FUNCTION, const std::string& dbgOurPeerName ) : m_PRETTY_FUNCTION(PRETTY_FUNCTION), m_dbgOurPeerName(dbgOurPeerName)
    {
        std::lock_guard<std::mutex> autolock( gLogMutex ); \
        std::cout << m_dbgOurPeerName << ": call: " << gIndentString+sizeof(gIndentString)-gCallLevel-1 << "->" << m_PRETTY_FUNCTION << std::endl << std::flush; \
        gCallLevel+=2;
    }
    
    ~FuncEntry()
    {
        gCallLevel-=2;
        std::lock_guard<std::mutex> autolock( gLogMutex ); \
        std::cout << m_dbgOurPeerName << ": call: " << gIndentString+sizeof(gIndentString)-gCallLevel-1 << "<-" << m_PRETTY_FUNCTION << std::endl << std::flush; \
    }
};
/*
#define _FUNC_ENTRY { \
        std::lock_guard<std::mutex> autolock( gLogMutex ); \
        std::cout << m_dbgOurPeerName << ": call: " << __PRETTY_FUNCTION__ << std::endl << std::flush; \
    }
*/

#define _FUNC_ENTRY ;
//#define _FUNC_ENTRY  FuncEntry funcEntry(__PRETTY_FUNCTION__,m_dbgOurPeerName);

#endif

#define SIRIUS_ASSERT(expr) { \
    if (!(expr)) {\
        std::lock_guard<std::mutex> autolock( gLogMutex ); \
        if (0) \
        std::cerr << m_dbgOurPeerName << ": " << __FILE__ << ":" << __LINE__ << " failed: " << current_time() << " " << #expr << "\n" << std::flush; \
        else \
        std::cerr << m_dbgOurPeerName << ": failed assert: " << current_time() << " " << #expr << "\n" << std::flush; \
        assert(0); \
    }\
}

#define _SIRIUS_ASSERT(expr) { \
    if (!(expr)) {\
        std::lock_guard<std::mutex> autolock( gLogMutex ); \
        std::cerr << __FILE__ << ":" << __LINE__ << " failed: " << current_time() << " " << #expr << "\n" << std::flush; \
        std::cerr << "failed assert!!!: " << current_time() << " " << #expr << "\n" << std::flush; \
        assert(0); \
    }\
}

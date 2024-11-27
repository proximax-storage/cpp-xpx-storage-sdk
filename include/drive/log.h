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

#ifdef USE_ELPP
    #include "easylogging/easylogging++.h"
    #define LOG_FOLDER "/tmp/replicator_service_logs"
#endif

BOOST_SYMBOL_EXPORT inline std::mutex gLogMutex;

BOOST_SYMBOL_EXPORT inline bool gSkipDhtPktLogs = false;
BOOST_SYMBOL_EXPORT inline bool gKademliaLogs = false;


inline uint64_t currentTimeSeconds()
{
    auto timeSinceEpoch = std::chrono::system_clock::now().time_since_epoch();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(timeSinceEpoch);
    return duration.count();
    
    
//    {
//        boost::posix_time::ptime universalTime = boost::posix_time::microsec_clock::universal_time();
//
//        static boost::posix_time::ptime epoch(boost::gregorian::date(1970, 1, 1));
//        boost::posix_time::time_duration duration = universalTime - epoch;
//
//        return static_cast<uint64_t>(duration.total_seconds());
//    }
}

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

//TODO:
//     std::ostringstream oss;
//     oss << std::setfill('0') << std::setw(4) << year << '.'
//         << std::setw(2) << month << '.'
//         << std::setw(2) << day << ' '
//         << std::setw(2) << hours << ':'
//         << std::setw(2) << minutes << ':'
//         << std::setw(2) << seconds << '.'
//         << std::setw(3) << milliseconds;
//
//     return oss.str();

    std::snprintf(buf, sizeof(buf), "%04ld.%02ld.%02ld %02ld:%02ld:%02ld.%03ld",
                  year, month, day, hours, minutes, seconds, milliseconds);

    return buf;
}

BOOST_SYMBOL_EXPORT inline bool gBreakOnWarning = false;
BOOST_SYMBOL_EXPORT inline bool gBreakOnError   = true;


BOOST_SYMBOL_EXPORT inline bool gIsRemoteRpcClient = false;
BOOST_SYMBOL_EXPORT inline std::optional<std::function<void()>> gCreateLogBackup = {};

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

#ifdef USE_ELPP
#include "plugins.h"

PLUGIN_API void setLogConf(std::string port);
PLUGIN_API void rolloutHandler(const char* absolutePath, std::size_t size);


#endif

#if USE_ELPP
#   define LOG(expr) {}
#else
#   define LOG(expr) {}
#endif

// _LOG - with m_dbgOurPeerName
//#define _LOG(expr) {}

#if USE_ELPP
#define _LOG(expr) { \
    std::lock_guard<std::mutex> autolock( gLogMutex ); \
    LOGPP(DEBUG) << current_time() << " " << m_dbgOurPeerName << ": " << expr; \
}
#else
#define _LOG(expr) { \
std::lock_guard<std::mutex> autolock( gLogMutex ); \
    checkLogFileSize(); \
    std::cout << current_time() << " " << m_dbgOurPeerName << ": " << expr << std::endl << std::flush; \
}
#endif


// __LOG
//#define __LOG(expr) {}
#if USE_ELPP
#define __LOG(expr) { \
    std::lock_guard<std::mutex> autolock( gLogMutex ); \
    LOGPP(DEBUG) << current_time() << " " << expr; \
}
#else
#define __LOG(expr) { \
std::lock_guard<std::mutex> autolock( gLogMutex ); \
    checkLogFileSize(); \
    std::cout << current_time() << " " << expr << std::endl << std::flush; \
}
#endif

// ___LOG
//#define ___LOG(expr) {}

#if USE_ELPP
#define ___LOG(expr) { \
    if ( !gKademliaLogs ) { \
        std::lock_guard<std::mutex> autolock( gLogMutex ); \
        LOGPP(DEBUG) << current_time() << " " << expr; \
    }   \
}
#else
#define ___LOG(expr) { \
if ( !gKademliaLogs ) {\
        std::lock_guard<std::mutex> autolock( gLogMutex ); \
        checkLogFileSize(); \
        std::cout << current_time() << " " << expr << std::endl << std::flush; \
}}
#endif

// _LOG_WARN - with m_dbgOurPeerName
#if USE_ELPP
#define _LOG_WARN(expr) { \
    std::lock_guard<std::mutex> autolock( gLogMutex ); \
    LOGPP(WARNING) << current_time() << " " << m_dbgOurPeerName << ": WARNING!!! in " << __FUNCTION__ << "() " << expr; \
    if ( gBreakOnWarning ) { assert(0); } \
}
#else
#define _LOG_WARN(expr) { \
        std::lock_guard<std::mutex> autolock( gLogMutex ); \
        checkLogFileSize(); \
        std::cout << current_time() << " " << m_dbgOurPeerName << ": WARNING!!! in " << __FUNCTION__ << "() " << expr << std::endl << std::flush; \
        if ( gBreakOnWarning ) { assert(0); } \
}
#endif

#if USE_ELPP
#define __LOG_WARN(expr) { \
    std::lock_guard<std::mutex> autolock( gLogMutex ); \
    LOGPP(WARNING) << ": WARNING!!! in " << __FUNCTION__ << "() " << current_time() << " " << expr; \
    if ( gBreakOnWarning ) { assert(0); } \
}
#else
#define __LOG_WARN(expr) { \
        std::lock_guard<std::mutex> autolock( gLogMutex ); \
        checkLogFileSize(); \
        std::cout << ": WARNING!!! in " << __FUNCTION__ << "() " << current_time() << " " << expr << std::endl << std::flush; \
        if ( gBreakOnWarning ) { assert(0); } \
    }
#endif

#if USE_ELPP
#define _LOG_ERR(expr) { \
    std::lock_guard<std::mutex> autolock( gLogMutex ); \
    LOGPP(ERROR) << __FILE__ << ":" << __LINE__ << ": " << __FUNCTION__ << ": "<< current_time() << " " << expr; \
    if ( gBreakOnError ) { assert(0); } \
 }
#else
#define _LOG_ERR(expr) { \
        std::lock_guard<std::mutex> autolock( gLogMutex ); \
        checkLogFileSize(); \
        std::cout << __FILE__ << ":" << __LINE__ << ": " << __FUNCTION__ << ": "<< current_time() << " " << expr << "\n" << std::flush; \
        if ( gBreakOnError ) { assert(0); } \
    }
#endif

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

//#define _FUNC_ENTRY ;
#if defined(_WIN32) || defined(_WIN64)
    #ifdef __MINGW32__ || __MINGW64__
        #define _FUNC_ENTRY  FuncEntry funcEntry(__PRETTY_FUNCTION__,m_dbgOurPeerName);
    #else
        #define _FUNC_ENTRY  FuncEntry funcEntry(__FUNCSIG__,m_dbgOurPeerName);
    #endif
#else
    #define _FUNC_ENTRY  FuncEntry funcEntry(__PRETTY_FUNCTION__,m_dbgOurPeerName);
#endif

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

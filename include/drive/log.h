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
    char buf[40];
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

static unsigned int idx;
#ifdef USE_ELPP
inline void rolloutHandler(const char* filename, std::size_t size)
{
// gLogFileName = logFolder / ("replicator_service_" + port + ".log");
    if (!std::filesystem::exists(LOG_FOLDER))
    {
        try {
            std::filesystem::create_directory(LOG_FOLDER);
            std::cout << "Directory 'LOG_FOLDER' created successfully." << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Error creating directory 'LOG_FOLDER': " << e.what() << std::endl;
        }
    } else {
        std::cout << "Directory 'LOG_FOLDER' already exists." << std::endl;
    }

    // SHOULD NOT LOG ANYTHING HERE BECAUSE LOG FILE IS CLOSED!
    std::cout << "************** Rolling out [" << filename << "] because it reached [" << size << " bytes]" << std::endl;

    std::stringstream ss;
    ss << "mv " << filename << " " << LOG_FOLDER << "/log-" << idx++ << ".log";
    std::cout << "************* command was: mv " << filename << " " << LOG_FOLDER << "/log-" << idx << ".log";
    system(ss.str().c_str());

    std::vector<std::filesystem::path> logFiles;
    for (const auto& entry : std::filesystem::directory_iterator("bin")) {
        if (entry.path().extension() == ".log") {
            logFiles.push_back(entry.path());
        }
    }
    // Sort the log files by creation time (oldest first)
    std::sort(logFiles.begin(), logFiles.end(), [](const std::filesystem::path& p1, const std::filesystem::path& p2) {
        return std::filesystem::last_write_time(p1) < std::filesystem::last_write_time(p2);
    });

    // Delete the oldest log files if there are more than 10
    if (logFiles.size() > 10) {
        for (std::size_t i = 0; i < logFiles.size() - 10; ++i) {
            std::cout << "Deleting oldest log file: " << logFiles[i] << std::endl;
            std::filesystem::remove(logFiles[i]);
        }
    }
}

inline void setLogConf(std::string port)
{
    el::Loggers::addFlag(el::LoggingFlag::StrictLogFileSizeCheck);
    el::Configurations conf;
    std::string filename = std::string(LOG_FOLDER) + "/ESLreplicator_service_" + port + ".log";
    conf.set(el::Level::Global, el::ConfigurationType::Filename, filename);
    conf.set(el::Level::Global, el::ConfigurationType::Format, "%msg");
    conf.set(el::Level::Global, el::ConfigurationType::SubsecondPrecision, "4");
    conf.set(el::Level::Global, el::ConfigurationType::ToFile, "true");
    conf.set(el::Level::Global, el::ConfigurationType::LogFlushThreshold, "1");
    conf.set(el::Level::Global, el::ConfigurationType::MaxLogFileSize, "10000");
    el::Loggers::reconfigureAllLoggers(conf);
}
#endif

#if USE_ELPP
#define LOG(expr) { \
    std::lock_guard<std::mutex> autolock( gLogMutex ); \
    checkLogFileSize(); \
    std::cout << current_time() << " " << std::endl << std::flush; \
}
#else
#define LOG(expr) {}
#endif

// _LOG - with m_dbgOurPeerName
//#define _LOG(expr) {}

#if USE_ELPP
#define _LOG(expr) { \
    std::lock_guard<std::mutex> autolock( gLogMutex ); \
    el::Helpers::installPreRollOutCallback(rolloutHandler); \
    LOGPP(DEBUG) <<"ESL______"<< current_time() << " " << m_dbgOurPeerName << ": " << expr; \
    el::Helpers::uninstallPreRollOutCallback(); \
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
    el::Helpers::installPreRollOutCallback(rolloutHandler); \
    LOGPP(DEBUG) <<"ESL______"<< current_time() << " " << expr; \
    el::Helpers::uninstallPreRollOutCallback(); \
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
        el::Helpers::installPreRollOutCallback(rolloutHandler); \
        LOGPP(DEBUG) <<"ESL______"<< current_time() << " " << expr; \
        el::Helpers::uninstallPreRollOutCallback(); \
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
    el::Helpers::installPreRollOutCallback(rolloutHandler); \
    LOGPP(WARNING) <<"ESL______"<< current_time() << " " << m_dbgOurPeerName << ": WARNING!!! in " << __FUNCTION__ << "() " << expr; \
    el::Helpers::uninstallPreRollOutCallback(); \
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
    el::Helpers::installPreRollOutCallback(rolloutHandler); \
    LOGPP(WARNING) <<"ESL______"<< ": WARNING!!! in " << __FUNCTION__ << "() " << current_time() << " " << expr; \
    el::Helpers::uninstallPreRollOutCallback(); \
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
    LOGPP(ERROR) <<"ESL______"<< __FILE__ << ":" << __LINE__ << ": " << __FUNCTION__ << ": "<< current_time() << " " << expr; \
    el::Helpers::uninstallPreRollOutCallback(); \
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
#ifdef _WIN64
    #define _FUNC_ENTRY  FuncEntry funcEntry(__FUNCSIG__,m_dbgOurPeerName);
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

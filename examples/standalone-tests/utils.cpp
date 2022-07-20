#include "utils.h"
#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>
#include <fstream>

namespace sirius::drive::test {

    std::mutex gExLogMutex;

    std::string now_str()
    {
        // Get current time from the clock, using microseconds resolution
        const boost::posix_time::ptime now =
                boost::posix_time::microsec_clock::local_time();

        // Get the time offset in current day
        const boost::posix_time::time_duration td = now.time_of_day();

        //
        // Extract hours, minutes, seconds and milliseconds.
        //
        // Since there is no direct accessor ".milliseconds()",
        // milliseconds are computed _by difference_ between total milliseconds
        // (for which there is an accessor), and the hours/minutes/seconds
        // values previously fetched.
        //
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
        sprintf(buf, "%02ld:%02ld:%02ld.%03ld",
                hours, minutes, seconds, milliseconds);

        return buf;
    }

    void clientSessionErrorHandler(const lt::alert *alert) {
        if (alert->type() == lt::listen_failed_alert::alert_type) {
            std::cerr << alert->message() << std::endl << std::flush;
            exit(-1);
        }
    }

    fs::path createClientFiles(const fs::path& clientFolder, size_t bigFileSize ) {

        // Create empty tmp folder for testing
        //
        fs::remove_all(clientFolder.parent_path() );
        fs::create_directories(clientFolder );
        //fs::create_directories( dataFolder/"empty_folder" );

        {
            std::ofstream file(clientFolder / "a.txt" );
            file.write( "a_txt", 5 );
        }
        {
            fs::path b_bin = clientFolder / "b.bin";
            fs::create_directories( b_bin.parent_path() );
            //        std::vector<uint8_t> data(10*1024*1024);
            std::ofstream file( b_bin );
            while (bigFileSize > 0) {
                // Max portion is 1GB
                auto portion = std::min(static_cast<unsigned long long>(bigFileSize), 1024ULL * 1024ULL * 1024ULL);
                std::vector<uint8_t> data(portion);
                std::generate( data.begin(), data.end(), std::rand );
                file.write( (char*) data.data(), data.size() );
                bigFileSize -= portion;
            }
        }
        {
            fs::path b_bin = clientFolder / "c.bin";
            fs::create_directories( b_bin.parent_path() );
            //        std::vector<uint8_t> data(10*1024*1024);
            std::vector<uint8_t> data(bigFileSize);
            std::generate( data.begin(), data.end(), std::rand );
            std::ofstream file( b_bin );
            file.write( (char*) data.data(), data.size() );
        }
        {
            fs::path d_bin = clientFolder / "d.bin";
            fs::create_directories( d_bin.parent_path() );
            //        std::vector<uint8_t> data(10*1024*1024);
            std::vector<uint8_t> data(bigFileSize);
            std::generate( data.begin(), data.end(), std::rand );
            std::ofstream file( d_bin );
            file.write( (char*) data.data(), data.size() );
        }
        {
            std::ofstream file(clientFolder / "c.txt" );
            file.write( "c_txt", 5 );
        }
        {
            std::ofstream file(clientFolder / "d.txt" );
            file.write( "d_txt", 5 );
        }

        // Return path to file
        return clientFolder.parent_path();
    }

    ActionList createActionList(const fs::path& clientRootFolder, uint64_t size) {
        fs::path clientFolder = clientRootFolder / "client_files";
        createClientFiles(clientFolder, size);
        ActionList actionList;
        actionList.push_back(Action::newFolder("fff1/"));
        actionList.push_back(Action::newFolder("fff1/ffff1"));
        actionList.push_back(Action::upload(clientFolder / "a.txt", "fff2/a.txt"));

        //actionList.push_back( Action::upload( clientFolder / "a.txt", "a.txt" ) );
        actionList.push_back(Action::upload(clientFolder / "a.txt", "a2.txt"));
        actionList.push_back(Action::upload(clientFolder / "b.bin", "f1/b1.bin"));
        actionList.push_back(Action::upload(clientFolder / "b.bin", "f2/b2.bin"));
        actionList.push_back(Action::upload(clientFolder / "a.txt", "f2/a.txt"));
        return actionList;
    }

    ActionList createActionList(const fs::path& clientRootFolder) {
        return createActionList(clientRootFolder, BIG_FILE_SIZE);
    }

    ActionList createActionList_2(const fs::path& clientRootFolder) {
        fs::path clientFolder = clientRootFolder / "client_files";
        ActionList actionList;
        actionList.push_back(Action::upload(clientFolder / "c.bin", "f1/c.bin"));
        return actionList;
    }

    ActionList createActionList_3(const fs::path& clientRootFolder) {
        fs::path clientFolder = clientRootFolder / "client_files";
        ActionList actionList;
        actionList.push_back(Action::upload(clientFolder / "d.bin", "d.bin"));
        return actionList;
    }
}

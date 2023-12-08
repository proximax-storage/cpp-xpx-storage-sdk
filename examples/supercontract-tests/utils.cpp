#include "utils.h"
#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>
#include <fstream>

namespace sirius::drive::test {

    std::mutex gExLogMutex;

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
            auto sizeLeft = bigFileSize;
            while (sizeLeft > 0) {
                // Max portion is 1GB
                auto portion = std::min(static_cast<unsigned long long>(sizeLeft), 1024ULL * 1024ULL * 1024ULL);
                std::vector<uint8_t> data(portion);
                std::generate( data.begin(), data.end(), std::rand );
                file.write( (char*) data.data(), data.size() );
                sizeLeft -= portion;
            }
        }
        {
            fs::path c_bin = clientFolder / "c.bin";
            fs::create_directories( c_bin.parent_path() );
            std::ofstream file( c_bin );
            auto sizeLeft = bigFileSize;
            while (sizeLeft > 0) {
                // Max portion is 1GB
                auto portion = std::min(static_cast<unsigned long long>(sizeLeft), 1024ULL * 1024ULL * 1024ULL);
                std::vector<uint8_t> data(portion);
                std::generate( data.begin(), data.end(), std::rand );
                file.write( (char*) data.data(), data.size() );
                sizeLeft -= portion;
                EXLOG( "SizeLeft " << sizeLeft );
            }
        }
        {
            fs::path d_bin = clientFolder / "d.bin";
            fs::create_directories( d_bin.parent_path() );
            std::ofstream file( d_bin );
            auto sizeLeft = bigFileSize;
            while (sizeLeft > 0) {
                // Max portion is 1GB
                auto portion = std::min(static_cast<unsigned long long>(sizeLeft), 1024ULL * 1024ULL * 1024ULL);
                std::vector<uint8_t> data(portion);
                std::generate( data.begin(), data.end(), std::rand );
                file.write( (char*) data.data(), data.size() );
                sizeLeft -= portion;
                EXLOG( "SizeLeft " << sizeLeft );
            }
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
        EXLOG( "Created action list" )
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

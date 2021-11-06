#include "utils.h"

namespace sirius::drive::test {

    const sirius::Hash256 modifyTransactionHash1 = std::array<uint8_t,32>{0xa1,0xf,0xf,0xf};
    sirius::crypto::KeyPair clientKeyPair = sirius::crypto::KeyPair::FromPrivate(
            sirius::crypto::PrivateKey::FromString("0000000000010203040501020304050102030405010203040501020304050102"));
    fs::path gClientFolder;
    std::shared_ptr<ClientSession> gClientSession;
    InfoHash clientModifyHash;
    std::mutex gExLogMutex;

    void clientSessionErrorHandler(const lt::alert *alert) {
        if (alert->type() == lt::listen_failed_alert::alert_type) {
            std::cerr << alert->message() << std::endl << std::flush;
            exit(-1);
        }
    }

    void clientModifyDrive(const ActionList &actionList,
                           const ReplicatorList &replicatorList,
                           const sirius::Hash256 &transactionHash) {
        actionList.dbgPrint();

        // Create empty tmp folder for 'client modify data'
        //
        auto tmpFolder = fs::temp_directory_path() / "modify_drive_data";
        // start file uploading
        InfoHash hash = gClientSession->addActionListToSession(actionList, replicatorList, transactionHash, tmpFolder);

        // inform replicator
        clientModifyHash = hash;

        EXLOG("# Client is waiting the end of replicator update");
    }

    fs::path createClientFiles( size_t bigFileSize ) {

        // Create empty tmp folder for testing
        //
        auto dataFolder = CLIENT_WORK_FOLDER / "client_files";
        fs::remove_all( dataFolder.parent_path() );
        fs::create_directories( dataFolder );
        //fs::create_directories( dataFolder/"empty_folder" );

        {
            std::ofstream file( dataFolder / "a.txt" );
            file.write( "a_txt", 5 );
        }
        {
            fs::path b_bin = dataFolder / "b.bin";
            fs::create_directories( b_bin.parent_path() );
            //        std::vector<uint8_t> data(10*1024*1024);
            std::vector<uint8_t> data(bigFileSize);
            std::generate( data.begin(), data.end(), std::rand );
            std::ofstream file( b_bin );
            file.write( (char*) data.data(), data.size() );
        }
        {
            std::ofstream file( dataFolder / "c.txt" );
            file.write( "c_txt", 5 );
        }
        {
            std::ofstream file( dataFolder / "d.txt" );
            file.write( "d_txt", 5 );
        }

        // Return path to file
        return dataFolder.parent_path();
    }

    ActionList createActionList() {
        fs::path clientFolder = gClientFolder / "client_files";
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
}
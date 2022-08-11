#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/array.hpp>
#include <boost/bind.hpp>
#include <ctime>
#include <syslog.h>
#include <unistd.h>
#include <filesystem>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <limits.h>
#endif

namespace fs = std::filesystem;

//#define DEBUG_NO_DAEMON_REPLICATOR_SERVICE

#define LOG_FOLDER "/tmp/replicator_service_logs"

#include "drive/RpcRemoteReplicator.h"
#include "drive/Replicator.h"

// signal_handler.cpp
void set_signal_handler();

int runServiceInBackground( fs::path, const std::string& );
int log( bool isError, const std::string& text );

char gExecutablePath[PATH_MAX+1] = { 0 };


bool runInBackground = false;
std::string address;
std::string port;

int main( int argc, char* argv[] )
{

#ifdef __linux__
    
    int nchar = readlink("/proc/self/exe", path, sizeof(path) );
    if ( nchar < 0 ) {
        _LOG_ERR("Invalid Read Link")
        return;
    }
    gExecutablePath[nchar] = 0;

#elif __APPLE__

    uint32_t bufsize = PATH_MAX;
    if( int rc = _NSGetExecutablePath( gExecutablePath, &bufsize); rc )
    {
        _LOG_ERR("Error: _NSGetExecutablePath: " << rc )
    }

#endif


#ifdef DEBUG_NO_DAEMON_REPLICATOR_SERVICE
//        runInBackground = true;
        address         = "127.0.0.1";
        port            = "5357";
#else
    if ( argc == 4 && std::string(argv[1])=="-d" )
    {
        __LOG( "argc == 4" )
        runInBackground = true;
        address         = argv[2];
        port            = argv[3];
    }
    else if ( argc == 3 )
    {
        __LOG( "argc == 3" )
        address         = argv[1];
        port            = argv[2];
    }
    else
    {
        std::cout << "usage: replicatro-service [-d] <address> <port>\n";
        //exit(0);
        runInBackground = true;
        address         = "127.0.0.1";
        port            = "5357";
    }
#endif

    __LOG( "replicator-service started: " << address << ":" << port )

    if ( runInBackground )
    {
        __LOG( "Run In Background" )
        runServiceInBackground(LOG_FOLDER, port);
        
        set_signal_handler();
    }

    __LOG( "RpcRemoteReplicator replicator" )
    sirius::drive::RpcRemoteReplicator replicator;
    replicator.run( address, port );
}

std::filesystem::path gLogFileName = "tmp/replicator_log"; // must be set

inline std::string openLogFile()
{
    log( false, " openLogFile: " + std::string(gLogFileName) );

    //
    // Send standard output to a log file.
    //
    const int flags = O_WRONLY | O_CREAT | O_APPEND;
    const mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
    if ( open( std::string(gLogFileName).c_str(), flags, mode) < 0 )
    {
        return "Unable to open output log file";
    }
    
    // Also send standard error to the same log file.
    if (dup(1) < 0)
    {
        return "Unable to dup output descriptor (into 'stderr')";
    }
    
    return {};
}

inline void createLogBackup()
{
    auto bakLogFile = gLogFileName.replace_extension("bak");
    if ( std::filesystem::exists(bakLogFile) )
    {
        std::filesystem::remove(bakLogFile);
    }

    close(0);
    close(1);
    
    std::filesystem::rename( gLogFileName, bakLogFile );

    auto error = openLogFile();
    if ( !error.empty() )
    {
        log( true, " openLogFile error: " + error );
    }
}


int runServiceInBackground( fs::path logFolder, const std::string& port )
{
    try
    {
        // Fork the process and have the parent exit. If the process was started
        // from a shell, this returns control to the user. Forking a new process is
        // also a prerequisite for the subsequent call to setsid().
        if (pid_t pid = fork())
        {
            if (pid > 0)
            {
                // We're in the parent process and need to exit.
                //
                // When the exit() function is used, the program terminates without
                // invoking local variables' destructors. Only global variables are
                // destroyed. As the io_service object is a local variable, this means
                // we do not have to call:
                //
                //   io_service.notify_fork(boost::asio::io_service::fork_parent);
                //
                // However, this line should be added before each call to exit() if
                // using a global io_service object. An additional call:
                //
                //   io_service.notify_fork(boost::asio::io_service::fork_prepare);
                //
                // should also precede the second fork().
                exit(0);
            }
            else
            {
                log( true, "First fork failed");
                exit(0);
            }
        }
        
        // Make the process a new session leader. This detaches it from the
        // terminal.
        setsid();
        
        // A process inherits its working directory from its parent. This could be
        // on a mounted filesystem, which means that the running daemon would
        // prevent this filesystem from being unmounted. Changing to the root
        // directory avoids this problem.
        chdir("/");
        
        // The file mode creation mask is also inherited from the parent process.
        // We don't want to restrict the permissions on files created by the
        // daemon, so the mask is cleared.
        //umask(0);
        
        // A second fork ensures the process cannot acquire a controlling terminal.
        if (pid_t pid = fork())
        {
            if (pid > 0)
            {
                exit(0);
            }
            else
            {
                log( true, "Second fork failed");
                exit(0);
            }
        }

        close(0);
        close(1);
        close(2);

        // We don't want the daemon to have any standard input.
        if ( open("/dev/null", O_RDONLY) < 0 )
        {
            log( true, "Unable to open /dev/null");
            exit(0);
        }
        
        // Check log folder
        std::error_code ec;
        fs::create_directories( logFolder, ec );
        
        if (ec)
        {
            log( true, "create_directories error");
        }
        
        fs::permissions( logFolder,
                        fs::perms::owner_all | fs::perms::group_all | fs::perms::others_all,
                        fs::perm_options::add );
        
        
        gLogFileName = logFolder / ("replicator_service_" + port + ".log");
        
        auto error = openLogFile();
        if ( !error.empty() )
        {
            log( true, " openLogFile error: " + error );
        }

        gCreateLogBackup = createLogBackup;
        gIsRemoteRpcClient = true;
        
        log( false, " Daemon  started" );
        log( false, " ---------------------------" );
        
        RPC_LOG( "Daemon started");
        RPC_LOG( "---------------------------------------------------------");
    }
    catch (std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << std::endl;
        RPC_LOG( "Exception: " << e.what() );
    }
    return 0;
}

int log( bool isError, const std::string& text )
{
    // init TCP socket
    int sockfd = socket( AF_INET, SOCK_STREAM, 0 );
    if (sockfd == -1)
    {
        __LOG( "log ERROR: sockfd == -1" )
        return 1;
    }

    // assign IP, PORT
    struct sockaddr_in servaddr;
    bzero( &servaddr, sizeof(servaddr) );
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr( address.c_str() );
    servaddr.sin_port = htons( std::stoi(port) );
   
    // connect
    if ( connect( sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr) ) != 0 )
    {
        __LOG( "log ERROR: connect" )
        return 2;
    }

    // send packet
    RPC_CMD command = isError ? RPC_CMD::log_err : RPC_CMD::log;
    write( sockfd, &command, sizeof(command) );
    uint16_t len = (uint16_t) text.size();
    write( sockfd, &len, sizeof(len) );
    write( sockfd, text.data(), text.length() );
    
    // close
    close(sockfd);

    return 0;
}

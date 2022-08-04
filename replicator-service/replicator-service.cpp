#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/array.hpp>
#include <boost/bind.hpp>
#include <ctime>
#include <syslog.h>
#include <unistd.h>

//#define DEBUG_NO_DAEMON_REPLICATOR_SERVICE

#define LOG_FILE "/tmp/replicator.daemon.log"
#undef LOG_FILE
#define LOG_FILE "/home/kyrylo/logs"

#include "drive/RpcRemoteReplicator.h"
#include "drive/Replicator.h"

int runServiceInBackground(std::string, std::string);

int main( int argc, char* argv[] )
{
    bool runInBackground = false;
    std::string address;
    std::string port;


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
        runServiceInBackground(LOG_FILE, port);
    }

    __LOG( "RpcRemoteReplicator replicator" )
    sirius::drive::RpcRemoteReplicator replicator;
    replicator.run( address, port );
}

int runServiceInBackground(std::string logFolder, std::string port)
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
                  std::cerr << "First fork failed\n";
                  return 1;
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
      umask(0);

      // A second fork ensures the process cannot acquire a controlling terminal.
      if (pid_t pid = fork())
      {
        if (pid > 0)
        {
          exit(0);
        }
        else
        {
            std::cerr << "Second fork failed\n";
          return 1;
        }
      }

      // Close the standard streams. This decouples the daemon from the terminal
      // that started it.
      close(0);
      close(1);
      close(2);

      // We don't want the daemon to have any standard input.
      if (open("/dev/null", O_RDONLY) < 0)
      {
        syslog(LOG_ERR | LOG_USER, "Unable to open /dev/null: %m");
        return 1;
      }

        // Send standard output to a log file.
	  	std::error_code ec;
	  	std::filesystem::create_directories(logFolder, ec);

		if (ec)
		{
			exit(0);
		}

        std::string output = logFolder + "replicator_service_" + port + ".log";
//        const int flags = O_WRONLY | O_CREAT | O_APPEND;
        const int flags = O_WRONLY | O_CREAT;
        const mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
        if (open(output.c_str(), flags, mode) < 0)
        {
          syslog(LOG_ERR | LOG_USER, "Unable to open output file %s: %m", output.c_str());
          return 1;
        }

        // Also send standard error to the same log file.
        if (dup(1) < 0)
        {
          syslog(LOG_ERR | LOG_USER, "Unable to dup output descriptor: %m");
          return 1;
        }

        RPC_LOG( "Daemon started");
        RPC_LOG( "--------------------------------------------------------");
    }
    catch (std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << std::endl;
        RPC_LOG( "Exception: " << e.what() );
    }
    return 0;
}

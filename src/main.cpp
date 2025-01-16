#include "wsserver/Listener.h"

#include <iostream>

int main(int argc, char* argv[])
{
    // Check command line arguments.
    if (argc != 4)
    {
        std::cerr <<
            "Usage: websocket-server-async <address> <port> <threads>\n" <<
            "Example:\n" <<
            "    websocket-server-async 0.0.0.0 8080 1\n";
        return EXIT_FAILURE;
    }
    auto const address = boost::asio::ip::make_address(argv[1]);
    auto const port = static_cast<unsigned short>(std::atoi(argv[2]));
    auto const threads = std::max<int>(1, std::atoi(argv[3]));

    // The io_context is required for all I/O
	boost::asio::io_context ioc{threads};

	const auto keyPair = sirius::crypto::KeyPair::FromString("7BF479B4D79F5752E6BCCE859D2D4731D898A9B16E90E6D479DF13CE15B8CD42");

    // Create and launch a listening port
    auto listener = std::make_shared<sirius::wsserver::Listener>(ioc, keyPair, "./storage_data");
	listener->init(boost::asio::ip::tcp::endpoint{address, port});
	listener->run();

    // Run the I/O service on the requested number of threads
    std::vector<std::thread> v;
    v.reserve(threads - 1);
    for(auto i = threads - 1; i > 0; --i)
        v.emplace_back(
        [&ioc]
        {
            ioc.run();
        });
    ioc.run();

    return EXIT_SUCCESS;
}
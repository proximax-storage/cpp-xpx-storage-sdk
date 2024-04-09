#include "wsserver/Listener.h"

#include <iostream>

int main(int argc, char* argv[])
{
    auto const address = net::ip::make_address("0.0.0.0");
    auto const port = 8081;
    //auto const threads = std::max<int>(1, std::atoi(argv[3]));
    auto const threads = 1;

    // The io_context is required for all I/O
    net::io_context ioc{threads};

    // Create and launch a listening port
    std::make_shared<sirius::wsserver::Listener>(ioc, tcp::endpoint{address, port})->run();

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
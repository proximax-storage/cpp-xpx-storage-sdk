#include "wsserver/Listener.h"


int main(int argc, char* argv[])
{
    auto const address = boost::asio::ip::make_address("0.0.0.0");
    auto const port = 8081;

    auto privateKey = sirius::crypto::PrivateKey::FromString("D6430327F90FAAD41F4BC69E51EB6C9D4C78B618D0A4B616478BD05E7A480950");
    auto keyPair = sirius::crypto::KeyPair::FromPrivate(std::move(privateKey));

    boost::asio::io_context ioc;
    const std::string storageDirectory = "storageDirectory";
    auto server = std::make_shared<sirius::wsserver::Listener>(ioc, keyPair, storageDirectory);
    server->init(boost::asio::ip::tcp::endpoint{address, port});
    server->run();

    unsigned int threadCount = std::thread::hardware_concurrency();
    std::vector<std::thread> threads;
    threads.reserve(threadCount);
    for(unsigned int i = 0; i < threadCount; i++ )
    {
        threads.emplace_back([&ioc]()
        {
            ioc.run();
        });
    }

    for (auto& thread : threads)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }

    return EXIT_SUCCESS;
}
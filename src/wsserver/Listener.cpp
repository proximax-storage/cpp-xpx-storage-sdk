#include "Listener.h"
#include "InSession.h"

void Listener::run()
{
    doAccept();
}

void Listener::doAccept()
{
    // The new connection gets its own strand
    acceptor.async_accept(
        net::make_strand(ioc),
        beast::bind_front_handler(
            &Listener::onAccept,
            shared_from_this()));
}

void Listener::onAccept(beast::error_code ec, tcp::socket socket)
{
    if(ec)
    {
        fail(ec, "accept");
    }
    else
    {
        // Create the Session and run it
        std::make_shared<InSession>(std::move(socket), strand)->run();
    }

    // Accept another connection
    doAccept();
}

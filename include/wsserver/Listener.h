#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/strand.hpp>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/beast/core/buffers_to_string.hpp>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <set>
#include <fstream>

#include "wsserver/Utils.h"

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace websocket = beast::websocket; // from <boost/beast/websocket.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>
namespace pt = boost::property_tree;    // from <boost/property_tree>

namespace sirius { namespace wsserver {
    // Accepts incoming connections and launches the Sessions
    class Listener : public std::enable_shared_from_this<Listener>
    {
        net::io_context& ioc;
        tcp::acceptor acceptor;
        net::io_context::strand strand;

    public:
        Listener(
            net::io_context& ioc,
            tcp::endpoint endpoint)
            : ioc(ioc)
            , acceptor(ioc)
            , strand(ioc)
        {
            beast::error_code ec;

            // Open the acceptor
            acceptor.open(endpoint.protocol(), ec);
            if(ec)
            {
                fail(ec, "open");
                return;
            }

            // Allow address reuse
            acceptor.set_option(net::socket_base::reuse_address(true), ec);
            if(ec)
            {
                fail(ec, "set_option");
                return;
            }

            // Bind to the server address
            acceptor.bind(endpoint, ec);
            if(ec)
            {
                fail(ec, "bind");
                return;
            }

            // Start listening for connections
            acceptor.listen(
                net::socket_base::max_listen_connections, ec);
            if(ec)
            {
                fail(ec, "listen");
                return;
            }
        }

        // Start accepting incoming connections
        void run();
        
    private:
        // Accepts incoming connections from client
        void doAccept();
        void onAccept(beast::error_code ec, tcp::socket socket);
    };
}}
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

#include "FilePath.h"

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace websocket = beast::websocket; // from <boost/beast/websocket.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>
namespace pt = boost::property_tree;    // from <boost/property_tree>

// Initialize OutSession if server makes connection to another server
class OutSession : public std::enable_shared_from_this<OutSession>
{
    tcp::resolver resolver;
    websocket::stream<beast::tcp_stream> ws;
    beast::flat_buffer buffer;
    std::string host;
    pt::ptree data;

public:
    // Resolver and socket require an io_context
    explicit
    OutSession(net::io_context& ioc)
        : resolver(net::make_strand(ioc))
        , ws(net::make_strand(ioc))
    {
    }

    // Start the asynchronous operation
    void run(char const* host, char const* port, FilePath filePath);
    void onResolve(beast::error_code ec, tcp::resolver::results_type results);
    void onConnect(beast::error_code ec, tcp::resolver::results_type::endpoint_type);
    void onHandshake(beast::error_code ec);
    void onWrite(beast::error_code ec, std::size_t bytes_transferred);
    void onWriteClose(beast::error_code ec, std::size_t bytes_transferred);
    void doRead();
    void onRead(beast::error_code ec, std::size_t bytes_transferred);
    void doClose();
    void onClose(beast::error_code ec);
    void sendMessage(pt::ptree* json, bool is_close);
    void sendData(pt::ptree* json);
    void recvData(pt::ptree* json);
    void recvDataChunk(pt::ptree* json);

    static std::set<std::shared_ptr<OutSession>> outgoing_sessions;

private:
    void handleJson(pt::ptree* parsed_pt);

    int send_numOfDataPieces = 0;
    const std::size_t send_dataPieceSize = 1024;
    int send_dataCounter = 0;
    std::string send_fileName = "";
    std::string send_filePath = "";

    std::unordered_map<std::string, std::string> recv_directory;
    std::unordered_map<std::string, int> recv_numOfDataPieces;
    std::unordered_map<std::string, int> recv_dataCounter;
};
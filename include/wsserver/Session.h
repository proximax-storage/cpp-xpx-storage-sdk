#ifndef SESSION_H
#define SESSION_H

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/beast/core/buffers_to_string.hpp>

#include <openssl/dh.h>

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

#define FILE_CHUNK_SIZE 1024*1024;

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace websocket = beast::websocket; // from <boost/beast/websocket.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>
namespace pt = boost::property_tree;    // from <boost/property_tree>

// Initialize Session when server accepts incoming connection from servers and clients
class Session : public std::enable_shared_from_this<Session> 
{
    websocket::stream<beast::tcp_stream> ws;
    beast::flat_buffer buffer;
    net::io_context::strand& strand;

public:
    // Take ownership of the socket
    explicit
    Session(tcp::socket&& socket, net::io_context::strand& strand)
        : ws(std::move(socket))
        , strand(strand)
    {
    }

    // Start the asynchronous operation
    void run();
    void onRun();
    void onAccept(beast::error_code ec);
    void doRead();
    void onRead(beast::error_code ec, std::size_t bytes_transferred);
    void onWrite(beast::error_code ec, std::size_t bytes_transferred);
    void onWriteClose(beast::error_code ec, std::size_t bytes_transferred);
    void doClose();
    void onClose(beast::error_code ec);
    void keyExchange(pt::ptree* json);
    void sendMessage(pt::ptree* json, bool is_close);
    void recvData(pt::ptree* json);
    void recvDataChunk(pt::ptree* json);
    void sendData(pt::ptree* json);
    void sendDataAck(pt::ptree* json);
    void deleteData(pt::ptree* json);

    void broadcastToAll(pt::ptree* json, bool is_close);
    void requestToAll(pt::ptree* json);
    
    // store connected sessions
    static std::set<std::shared_ptr<Session>> incoming_sessions;

private:
    void handleJson(pt::ptree* parsed_pt);

    std::string sharedKey;

    std::unordered_map<std::string, std::string>  recv_directory;
    std::unordered_map<std::string, int> recv_numOfDataPieces;
    std::unordered_map<std::string, int> recv_dataCounter;
    
    std::unordered_map<std::string, int> send_numOfDataPieces;
    const std::size_t send_DataPieceSize = FILE_CHUNK_SIZE;
    std::unordered_map<std::string, int> send_dataCounter;
};

#endif // SESSION_H

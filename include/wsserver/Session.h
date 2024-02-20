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

    // Accept connections
    void onAccept(beast::error_code ec);

    // Receive JSON from the client
    void doRead();
    // Triggered after receive JSON from the client
    void onRead(beast::error_code ec, std::size_t bytes_transferred);

    // Triggered after sending JSON to the client
    void onWrite(beast::error_code ec, std::size_t bytes_transferred);
    void onWriteClose(beast::error_code ec, std::size_t bytes_transferred);

    // Close connection
    void doClose();
    // Triggered after connection is closed
    void onClose(beast::error_code ec);

    // Perform ky exchange using Diffie-Hellman
    void keyExchange(pt::ptree* json);

    // Send JSON to client
    void sendMessage(pt::ptree* json, bool is_close);

    // Receive binary data from client (Client upload)
    void recvData(pt::ptree* json);
    void recvDataChunk(pt::ptree* json);

    // Send binary data to client (Client download)
    void sendData(pt::ptree* json);
    void sendDataAck(pt::ptree* json);

    // Delete stored data in server
    void deleteData(pt::ptree* json);

    void broadcastToAll(pt::ptree* json, bool is_close);
    void requestToAll(pt::ptree* json);
    
    // store connected sessions
    static std::set<std::shared_ptr<Session>> incoming_sessions;

private:
    // Handle JSON received from client
    void handleJson(pt::ptree* parsed_pt);

    std::string sharedKey; // Shared secret key for encrypt/decrypt/hashing

    std::unordered_map<std::string, std::string>  recv_directory;
    std::unordered_map<std::string, int> recv_numOfDataPieces;
    std::unordered_map<std::string, int> recv_dataCounter;
    
    std::unordered_map<std::string, int> send_numOfDataPieces;
    const std::size_t send_DataPieceSize = FILE_CHUNK_SIZE;
    std::unordered_map<std::string, int> send_dataCounter;
};

#endif // SESSION_H

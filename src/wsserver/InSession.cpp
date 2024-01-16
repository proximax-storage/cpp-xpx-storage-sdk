#include "wsserver/InSession.h"
#include "wsserver/OutSession.h"
#include "wsserver/Utils.h"
#include "wsserver/Task.h"
#include "wsserver/Base64.h"
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

std::set<std::shared_ptr<InSession>> InSession::incoming_sessions;
std::set<std::shared_ptr<InSession>> InSession::incoming_sessions_client;
std::set<std::shared_ptr<InSession>> InSession::incoming_sessions_server;

void InSession::run()
{
    // Set suggested timeout settings for the websocket
    ws.set_option(
        websocket::stream_base::timeout::suggested(
            beast::role_type::server));

    // Set a decorator to change the Server of the handshake
    ws.set_option(websocket::stream_base::decorator(
        [](websocket::response_type& res)
        {
            res.set(http::field::server,
                std::string(BOOST_BEAST_VERSION_STRING) +
                    " websocket-server-async");
        }));

    // Accept the websocket handshake
    ws.async_accept(
        beast::bind_front_handler(
            &InSession::onAccept,
            shared_from_this()));
}

void InSession::onAccept(beast::error_code ec)
{
    if(ec)
        return fail(ec, "accept");
    
    // Keep track of connected sessions
    incoming_sessions.insert(shared_from_this());

    std::cout << "Accepted Sessions:\n";
    for (const auto& sess : incoming_sessions) {
        std::cout << "Session ID: " << sess.get() << "\n";
    }

    // Read a message
    doRead();
}

void InSession::doRead()
{
    // Read a message into our buffer
    ws.async_read(
        buffer,
        beast::bind_front_handler(
            &InSession::onRead,
            shared_from_this()));
}

void InSession::onRead(beast::error_code ec, std::size_t bytes_transferred)
{
    boost::ignore_unused(bytes_transferred);

    // This indicates that the Session was closed
    if(ec == websocket::error::closed)
        return;

    if(ec) {
        fail(ec, "read");
        doClose();
    }
    else {
        std::string message = boost::beast::buffers_to_string(buffer.data());
        buffer.consume(buffer.size());

        try {
            boost::property_tree::ptree parsed_pt;
            std::istringstream json_input(message);
            read_json(json_input, parsed_pt);
            handleJson(&parsed_pt);

        } catch (const boost::property_tree::json_parser_error& e) {
            std::cerr << "JSON parsing error: " << e.what() << std::endl;
            doClose();
        } catch (const std::exception& e) {
            std::cerr << "An unexpected error occurred: " << e.what() << std::endl;
            doClose();
        } catch (...) {
            std::cerr << "An unexpected and unknown error occurred." << std::endl;
            doClose();
        }
    }
}

void InSession::onWrite(beast::error_code ec, std::size_t bytes_transferred)
{
    boost::ignore_unused(bytes_transferred);

    if(ec)
        fail(ec, "write");
}

void InSession::onWriteClose(beast::error_code ec, std::size_t bytes_transferred)
{
    boost::ignore_unused(bytes_transferred);

    if(ec)
        fail(ec, "write");

    // Clear the buffer
    buffer.consume(buffer.size());

    doClose();
}

void InSession::doClose()
{
    ws.async_close(websocket::close_code::normal,
        beast::bind_front_handler(
            &InSession::onClose,
            shared_from_this()));
}
void InSession::onClose(beast::error_code ec) 
{
    if (ec) {
        fail(ec, "close");
    } else {
        std::cout << "Session ID: " << shared_from_this().get() << " is closed successfully." << std::endl;
    }

    incoming_sessions.erase(shared_from_this());
    incoming_sessions_client.erase(shared_from_this());
    incoming_sessions_server.erase(shared_from_this());
}

// Send message to a client
void InSession::sendMessage(pt::ptree* json, bool is_close) 
{
    std::ostringstream message;
    write_json(message, *json, false);

    ws.async_write(
        net::buffer(message.str()),
        beast::bind_front_handler(
            is_close ? &InSession::onWriteClose : &InSession::onWrite,
            shared_from_this()));
}

void InSession::recvData(pt::ptree* json)
{
    std::string uid = (*json).get<std::string>("uid");
    recv_directory[uid] = "saved-data/" 
        + (*json).get<std::string>("drive") 
        + (*json).get<std::string>("directory");

    try {
        fs::create_directories(recv_directory[uid]);
        std::cout << "Folder created successfully: " << recv_directory[uid] << std::endl;
    } catch (const std::filesystem::filesystem_error& ex) {
        std::cerr << "Error creating folder: " << ex.what() << std::endl;
    }

    recv_directory[uid]  += (*json).get<std::string>("fileName");

    // Check if the file exists
    if (fs::exists(recv_directory[uid])) {
        // Delete the existing file
        if (fs::remove(recv_directory[uid])) {
            std::cout << "Exisiting file deleted for upload: " << recv_directory[uid]  << std::endl;
        } else {
            std::cerr << "Error deleting existing file for upload: " << recv_directory[uid]  << std::endl;
            return;
        }
    }

    recv_numOfDataPieces[uid] = (*json).get<int>("numOfDataPieces");
    recv_dataCounter[uid] = 0;

    doRead();
}

void InSession::recvDataChunk(pt::ptree* json) {
    std::string uid = (*json).get<std::string>("uid");

    std::cout << recv_dataCounter[uid] << std::endl;
    std::string binaryString = base64_decode((*json).get<std::string>("data"));     
    std::ofstream file(recv_directory[uid], std::ios::binary | std::ios::app);
    file << binaryString;
    file.close();

    if (recv_dataCounter[uid] + 1 == recv_numOfDataPieces[uid]) {
        pt::ptree done;
        done.put("isClient", false);
        done.put("serverRecv", true);
        done.put("task", Task::UPLOAD_ACK);
        done.put("uid", uid);

        sendMessage(&done, false);
        std::cout << "Succesfuly uploaded to server: " << recv_directory[uid] << std::endl;

        recv_directory[uid] = "";
        recv_dataCounter[uid] = 0;

        auto it = recv_directory.find(uid);
        auto it2 = recv_dataCounter.find(uid);
        auto it3 = recv_numOfDataPieces.find(uid);

        if (it != recv_directory.end() && it2 != recv_dataCounter.end() && it3 != recv_numOfDataPieces.end()) {
            recv_directory.erase(it);
            recv_dataCounter.erase(it2);
            recv_numOfDataPieces.erase(it3);
            std::cout << "Upload uid: " << uid << " has been deleted" << std::endl;
        } else {
            std::cerr << "Error deleting upload uid: " << uid << std::endl;
        }
    }
    else {
        recv_dataCounter[uid]++;
    }     

    doRead(); 
}

void InSession::sendData(pt::ptree* json) {
    std::string fileName = (*json).get<std::string>("fileName");
    std::string filePath = "get-data/" 
        + (*json).get<std::string>("drive") 
        + (*json).get<std::string>("directory") 
        + fileName;

    std::string uid = (*json).get<std::string>("uid");

    send_numOfDataPieces[uid] = std::filesystem::file_size(filePath) / send_DataPieceSize;
    if (std::filesystem::file_size(filePath) % send_DataPieceSize != 0) {
        send_numOfDataPieces[uid]++;
    }

    (*json).put("isClient", false);
    (*json).put("task", Task::DOWNLOAD_INFO);
    (*json).put("dataSize", std::filesystem::file_size(filePath));
    (*json).put("dataPieceSize", send_DataPieceSize);
    (*json).put("numOfDataPieces", send_numOfDataPieces[uid]);
    (*json).put("uid", uid);
    std::ostringstream send_stream;
    write_json(send_stream, (*json), false);

    send_dataCounter[uid]  = 0;
    
    ws.async_write(net::buffer(send_stream.str()),
        [this, uid, filePath, fileName](beast::error_code ec, std::size_t bytes_transferred) {
            std::cout << "Download info sent: " << filePath << std::endl;
            if (!ec) {
                std::vector<char> fileBuffer(send_DataPieceSize, 0);
                std::ifstream file;
                file.open(filePath, std::ios::binary);
                if (!file.is_open()) {
                    std::cerr << "[Download info] Failed to open file: " << filePath << std::endl;
                    return;
                }

                while (!file.eof()) {
                    file.read(fileBuffer.data(), fileBuffer.size());
                    std::streamsize bytesRead = file.gcount();

                    std::ofstream outputFile("out_" + fileName, std::ios::binary);
                    outputFile.write(fileBuffer.data(), bytesRead);
                    outputFile.close();

                    std::ifstream outputFile2("out_" + fileName, std::ios::binary);
                    std::ostringstream ss;
                    ss << outputFile2.rdbuf();
                    outputFile2.close();
                    std::string bin_data = base64_encode(ss.str());
                    
                    pt::ptree data;
                    data.put("isClient", false);
                    data.put("task", Task::DOWNLOAD_DATA);
                    data.put("data", bin_data);
                    data.put("dataPieceNum", send_dataCounter[uid]);
                    data.put("uid", uid);
                    std::ostringstream data_stream;
                    write_json(data_stream, data, false);
                    ws.write(net::buffer(data_stream.str()));
                    
                    send_dataCounter[uid]++;
                    fileBuffer.assign(send_DataPieceSize, 0);
                }
                
                try {
                    std::filesystem::remove("out_" + fileName);
                } catch (const std::filesystem::filesystem_error& e) {
                    std::cerr << "Error deleting file: " << e.what() << std::endl;
                }

            } else {
                // Handle errors or connection closure
                std::cerr << "Error in sending data: " << ec.message() << std::endl;
                doClose();
            }
    });

    doRead();
}

void InSession::sendDataAck(pt::ptree* json) {
    std::string uid = (*json).get<std::string>("uid");

    auto it = send_numOfDataPieces.find(uid);
    auto it2 = send_dataCounter.find(uid);

    if (it != send_numOfDataPieces.end() && it2 != send_dataCounter.end()) {
        send_numOfDataPieces.erase(it);
        send_dataCounter.erase(it2);
        std::cout << "Download uid: " << uid << " has been deleted" << std::endl;
    } else {
        std::cerr << "Error deleting download uid: " << uid << std::endl;
    }
}

void InSession::deleteData(pt::ptree* json) {
    std::string deleteFilePath = "saved-data/" 
        + (*json).get<std::string>("drive") 
        + (*json).get<std::string>("directory") 
        + (*json).get<std::string>("fileName");

    // Check if the file exists
    if (fs::exists(deleteFilePath)) {
        // Delete the existing file
        if (fs::remove(deleteFilePath)) {
            std::cout << "File deleted: " << deleteFilePath << std::endl;
            (*json).put("task", Task::DELETE_DATA_ACK);
        } else {
            (*json).put("task", Task::DELETE_DATA_FAILURE);
            std::cerr << "Error deleting file: " << deleteFilePath << std::endl;
        }
    } else {
        std::cout << "File does not exist: " << deleteFilePath << std::endl;
        (*json).put("task", Task::DELETE_DATA_FAILURE);
    }

    (*json).put("isClient", false);
    std::ostringstream deleteData_stream;
    write_json(deleteData_stream, *json, false);

    ws.async_write(net::buffer(deleteData_stream.str()), 
        [](beast::error_code ec, std::size_t bytes_transferred){
    });

    doRead();
};

// Broadcast message to all InSession
void InSession::broadcastToAll(pt::ptree* json, bool is_close) 
{
    for (const auto& session : incoming_sessions) {
        session->sendMessage(json, is_close);
    }
}

// Broadcast message to all clients
void InSession::broadcastToClients(pt::ptree* json, bool is_close) 
{
    for (const auto& session : incoming_sessions_client) {
        session->sendMessage(json, is_close);
    }
}

// Broadcast message to all clients
void InSession::broadcastToServers(pt::ptree* json, bool is_close) 
{
    for (const auto& session : incoming_sessions_server) {
        session->sendMessage(json, is_close);
    }
}

// Make a request to all InSession
void InSession::requestToAll(pt::ptree* json) 
{
    for (const auto& session : incoming_sessions) {
        session->sendMessage(json, false);
        session->doRead();
    }
}

// Make a request to all clients
void InSession::requestToClients(pt::ptree* json) 
{
    for (const auto& session : incoming_sessions_client) {
        session->sendMessage(json, false);
        session->doRead();
    }
}

// Make a request to all clients
void InSession::requestToServers(pt::ptree* json) 
{
    for (const auto& session : incoming_sessions_server) {
        session->sendMessage(json, false);
        session->doRead();
    }
}

void InSession::connectToServer(const char* host, const char* port, FilePath filePath)
{
    net::io_context ioc;
    std::make_shared<OutSession>(ioc)->run(host, port, filePath);
    ioc.run();
}

// Handles JSON receive by doRead()
void InSession::handleJson(pt::ptree* parsed_pt)
{
    bool isClient = (*parsed_pt).get<bool>("isClient");
    int task = (*parsed_pt).get<int>("task");
    std::ostringstream json_output;
    std::ostringstream print;
    pt::ptree close;

    if (isClient) {
        std::cout << "Receive from a client" << std::endl;
        incoming_sessions_client.insert(shared_from_this());

        std::cout << "Client Sessions:\n";
        for (const auto& sess : incoming_sessions_client) {
            std::cout << "Session ID: " << sess.get() << "\n";
        }

        net::io_context ioc;
        
        switch (task) {
            case DOWNLOAD_START:
                write_json(print, *parsed_pt);
                std::cout << print.str() << std::endl;
                sendData(parsed_pt);
                break;
            case DOWNLOAD_FAILURE:
                write_json(print, *parsed_pt);
                std::cout << print.str() << std::endl;
                sendData(parsed_pt);    
                break;
            case DOWNLOAD_ACK:
                sendDataAck(parsed_pt);
                doRead();
                break;
            case UPLOAD_START:
                write_json(print, *parsed_pt);
                std::cout << print.str() << std::endl;
                recvData(parsed_pt);
                break;
            case UPLOAD_DATA:
                recvDataChunk(parsed_pt);
                break;
            case UPLOAD_FAILURE:
                write_json(print, *parsed_pt);
                std::cout << print.str() << std::endl;
                recvData(parsed_pt);
                break;
            case DELETE_DATA:
                write_json(print, *parsed_pt);
                std::cout << print.str() << std::endl;
                deleteData(parsed_pt);
                break;
            case MESSAGE:
                write_json(print, *parsed_pt);
                std::cout << print.str() << std::endl;
                (*parsed_pt).put("serverRecv", true);
                (*parsed_pt).put("task", MESSAGE_ACK);
                write_json(json_output, *parsed_pt, false);
                sendMessage(parsed_pt, false);
                doRead();
                break;
            case CLOSE:
                write_json(print, *parsed_pt);
                std::cout << print.str() << std::endl;
                close.put("isClient", false);
                close.put("task", CLOSE_ACK);
                close.put("serverRecv", true);
                sendMessage(&close, true);
                break;
            case CLOSE_ACK:
                doClose();
                break;
            default:
                doClose();
        }
    } else {
        std::cout << "Receive from a server" << std::endl;
        incoming_sessions_server.insert(shared_from_this());

        std::cout << "Server Sessions:\n";
        for (const auto& sess : incoming_sessions_server) {
            std::cout << "Session ID: " << sess.get() << "\n";
        }

        switch (task) {
            case DOWNLOAD_START:
                write_json(print, *parsed_pt);
                std::cout << print.str() << std::endl;
                sendData(parsed_pt);
                break;
            case DOWNLOAD_ACK:
                sendDataAck(parsed_pt);
                doRead();
                break;
            case DOWNLOAD_FAILURE:
                write_json(print, *parsed_pt);
                std::cout << print.str() << std::endl;
                sendData(parsed_pt);
                break;
            case UPLOAD_START:
                write_json(print, *parsed_pt);
                std::cout << print.str() << std::endl;
                recvData(parsed_pt);
                break;
            case UPLOAD_DATA:
                recvDataChunk(parsed_pt);
                break;
            case UPLOAD_FAILURE:
                write_json(print, *parsed_pt);
                std::cout << print.str() << std::endl;
                recvData(parsed_pt);
                break;
            case MESSAGE:
                write_json(print, *parsed_pt);
                std::cout << print.str() << std::endl;
                (*parsed_pt).put("serverRecv", true);
                (*parsed_pt).put("task", MESSAGE_ACK);
                write_json(json_output, *parsed_pt, false);
                sendMessage(parsed_pt, false);
                doRead();
                break;
            case CLOSE:
                write_json(print, *parsed_pt);
                std::cout << print.str() << std::endl;
                close.put("isClient", false);
                close.put("task", CLOSE_ACK);
                close.put("serverRecv", true);
                sendMessage(&close, true);
                break;
            case CLOSE_ACK:
                doClose();
                break;
            default:
                doClose();
        }

    }
    
    return;
}

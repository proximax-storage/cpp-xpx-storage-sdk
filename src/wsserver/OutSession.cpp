#include "wsserver/OutSession.h"
#include "wsserver/Utils.h"
#include "wsserver/Task.h"
#include "wsserver/Base64.h"
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

std::set<std::shared_ptr<OutSession>> OutSession::outgoing_sessions;

void OutSession::run(char const* host, char const* port, FilePath filePath)
{
    // Save these for later
    host = host;

    // Look up the domain name
    resolver.async_resolve(
        host,
        port,
        beast::bind_front_handler(
            &OutSession::onResolve,
            shared_from_this()));

    // pt::ptree msg;
    // msg.put("isClient", false);
    // msg.put("drive", filePath.getDrive());
    // msg.put("directory", filePath.getFolder());
    // msg.put("fileName", filePath.getFileName());
    // msg.put("uid", filePath.getUid());
    // sendData(&msg);

    pt::ptree msg;
    msg.put("isClient", false);
    msg.put("task", MESSAGE);
    msg.put("serverRecv", false);
    msg.put("msg", "Hello World!");
    sendMessage(&msg, false);
}

void OutSession::onResolve(beast::error_code ec, tcp::resolver::results_type results)
{
    if(ec)
        return fail(ec, "resolve");

    // Set the timeout for the operation
    beast::get_lowest_layer(ws).expires_after(std::chrono::seconds(30));

    // Make the connection on the IP address we get from a lookup
    beast::get_lowest_layer(ws).async_connect(
        results,
        beast::bind_front_handler(
            &OutSession::onConnect,
            shared_from_this()));
}

void OutSession::onConnect(beast::error_code ec, tcp::resolver::results_type::endpoint_type)
{
    if(ec)
        return fail(ec, "connect");

    // Turn off the timeout on the tcp_stream, because
    // the websocket stream has its own timeout system.
    beast::get_lowest_layer(ws).expires_never();

    // Set suggested timeout settings for the websocket
    ws.set_option(
        websocket::stream_base::timeout::suggested(
            beast::role_type::client));

    // Set a decorator to change the User-Agent of the handshake
    ws.set_option(websocket::stream_base::decorator(
        [](websocket::request_type& req)
        {
            req.set(http::field::user_agent,
                std::string(BOOST_BEAST_VERSION_STRING) +
                    " websocket-client-async");
        }));

    // Perform the websocket handshake
    ws.async_handshake(host, "/",
        beast::bind_front_handler(
            &OutSession::onHandshake,
            shared_from_this()));
}

void OutSession::onHandshake(beast::error_code ec)
{
    if(ec)
        return fail(ec, "handshake");

    outgoing_sessions.insert(shared_from_this());

    std::cout << "Outgoing Sessions:\n";
    for (const auto& sess : outgoing_sessions) {
        std::cout << "Session ID: " << sess.get() << "\n";
    }
}

void OutSession::onWrite(beast::error_code ec, std::size_t bytes_transferred)
{
    boost::ignore_unused(bytes_transferred);

    if(ec)
        return fail(ec, "write");
}

void OutSession::onWriteClose(beast::error_code ec, std::size_t bytes_transferred)
{
    boost::ignore_unused(bytes_transferred);

    if(ec)
        fail(ec, "write");

    // Clear the buffer
    buffer.consume(buffer.size());

    doClose();
}

void OutSession::doRead()
{
    // Read a message into our buffer
    ws.async_read(
        buffer,
        beast::bind_front_handler(
            &OutSession::onRead,
            shared_from_this()));
}

void OutSession::onRead(beast::error_code ec, std::size_t bytes_transferred)
{
    boost::ignore_unused(bytes_transferred);

    // This indicates that the Session was closed
    if(ec == websocket::error::closed)
        return;

    if(ec)
        fail(ec, "read");
    
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

void OutSession::doClose()
{
    // Close the WebSocket connection
    ws.async_close(websocket::close_code::normal,
        beast::bind_front_handler(
            &OutSession::onClose,
            shared_from_this()));
}

void OutSession::onClose(beast::error_code ec)
{
    if(ec)
        fail(ec, "close");
    else
        std::cout << "WebSocket closed successfully." << std::endl;

    outgoing_sessions.erase(shared_from_this());
}

// Send message to a client (does not trigger on write)
void OutSession::sendMessage(pt::ptree* json, bool is_close) 
{
    // ws_.text(ws_.got_text());
    std::ostringstream message;
    write_json(message, *json, false);

    ws.async_write(
        net::buffer(message.str()),
        beast::bind_front_handler(
            is_close ? &OutSession::onWriteClose : &OutSession::onWrite,
            shared_from_this()));
}

void OutSession::sendData(pt::ptree* json) {
    std::string drive = "get-data/" + (*json).get<std::string>("drive");
    std::string directory = (*json).get<std::string>("directory");
    send_fileName = (*json).get<std::string>("fileName");
    send_filePath = drive + directory + send_fileName;

    std::string uid = (*json).get<std::string>("uid");

    send_numOfDataPieces = std::filesystem::file_size(send_filePath) / send_dataPieceSize;
    if (std::filesystem::file_size(send_filePath) % send_dataPieceSize != 0) {
        send_numOfDataPieces++; // Add one more substring for the remaining characters
    }

    (*json).put("isClient", false);
    (*json).put("task", Task::UPLOAD_START);
    (*json).put("dataSize", std::filesystem::file_size(send_filePath));
    (*json).put("dataPieceSize", send_dataPieceSize);
    (*json).put("numOfDataPieces", send_numOfDataPieces);

    std::ostringstream send_stream;
    write_json(send_stream, *json, false);

    send_dataCounter = 0;
    
    ws.async_write(net::buffer(send_stream.str()),
        [this, &uid](beast::error_code ec, std::size_t bytes_transferred) {
            if (!ec) {
                std::vector<char> fileBuffer(send_dataPieceSize, 0);
                std::ifstream file;
                file.open(send_filePath, std::ios::binary);
                if (!file.is_open()) {
                    std::cerr << "Failed to open file: " << send_filePath << std::endl;
                    return;
                }

                while (!file.eof()) {
                    file.read(fileBuffer.data(), fileBuffer.size());
                    std::streamsize bytesRead = file.gcount();

                    std::ofstream outputFile("out_" + send_fileName, std::ios::binary);
                    outputFile.write(fileBuffer.data(), bytesRead);
                    outputFile.close();

                    std::ifstream outputFile2("out_" + send_fileName, std::ios::binary);
                    std::ostringstream ss;
                    ss << outputFile2.rdbuf();
                    outputFile2.close();
                    std::string bin_data = base64_encode(ss.str());
                    
                    pt::ptree data;
                    data.put("isClient", false);
                    data.put("task", Task::UPLOAD_DATA);
                    data.put("data", bin_data);
                    data.put("dataPieceNum", send_dataCounter);
                    data.put("uid", uid);
                    std::ostringstream data_stream;
                    write_json(data_stream, data, false);

                    ws.write(net::buffer(data_stream.str()));
                    send_dataCounter++;
                    fileBuffer.assign(send_dataPieceSize, 0);
                }
                
                try {
                    std::filesystem::remove("out_" + send_fileName);
                    std::cout << "File deleted successfully." << std::endl;
                } catch (const std::filesystem::filesystem_error& e) {
                    std::cerr << "Error deleting file: " << e.what() << std::endl;
                }
                
                doRead();

            } else {
                // Handle errors or connection closure
                std::cerr << "Error in async_write: " << ec.message() << std::endl;
                doClose();
            }
        });
}

void OutSession::recvData(pt::ptree* json)
{
    std::string uid = (*json).get<std::string>("uid");
    recv_directory[uid] = "saved-data/";
    recv_directory[uid] += (*json).get<std::string>("drive");
    recv_directory[uid] += (*json).get<std::string>("directory");
    
    try {
        fs::create_directories(recv_directory[uid]);
        std::cout << "Folder created successfully: " << recv_directory[uid] << std::endl;
    } catch (const std::filesystem::filesystem_error& ex) {
        std::cerr << "Error creating folder: " << ex.what() << std::endl;
    }

    recv_directory[uid] += (*json).get<std::string>("fileName");

    // Check if the file exists
    if (fs::exists(recv_directory[uid])) {
        // Delete the existing file
        if (fs::remove(recv_directory[uid])) {
            std::cout << "File deleted: " << recv_directory[uid] << std::endl;
        } else {
            std::cerr << "Error deleting file: " << recv_directory[uid] << std::endl;
            return;
        }
    }

    recv_numOfDataPieces[uid] = (*json).get<int>("numOfDataPieces");
    doRead();
}

void OutSession::recvDataChunk(pt::ptree* json) {
    std::string uid = (*json).get<std::string>("uid");

    std::string binaryString = base64_decode((*json).get<std::string>("data"));     
    std::ofstream file(recv_directory[uid], std::ios::binary | std::ios::app);
    file << binaryString;
    file.close();

    std::cout << recv_dataCounter[uid] << std::endl;
    if (recv_dataCounter[uid] + 1 == recv_numOfDataPieces[uid]) {
        pt::ptree done;
        done.put("isClient", true);
        done.put("serverRecv", true);
        done.put("task", Task::DOWNLOAD_ACK);

        std::ostringstream message;
        write_json(message, done, false);
        ws.write(net::buffer(message.str()));

        std::cout << "Binary data saved" << std::endl;

        recv_directory[uid] = "";
        recv_dataCounter[uid] = 0;
        doRead();
    }
    else {
        recv_dataCounter[uid]++;
        doRead();
    }            
}

void OutSession::handleJson(pt::ptree* parsed_pt)
{
    int task = (*parsed_pt).get<int>("task");
    std::cout << "Outgoing Sessions:\n";
    for (const auto& sess : outgoing_sessions) {
        std::cout << "Session ID: " << sess.get() << "\n";
    }
    std::ostringstream json_output;
    write_json(json_output, *parsed_pt, false);
    std::cout << json_output.str() << std::endl;

    pt::ptree close;
    close.put("isClient", false);
    close.put("task", CLOSE);
    close.put("serverRecv", false);

    switch (task) {
        case DOWNLOAD_INFO:
            recvData(parsed_pt);
            break;
        case DOWNLOAD_DATA:
            recvDataChunk(parsed_pt);
            break;
        case DOWNLOAD_FAILURE:
            recvData(parsed_pt);
            break;
        case UPLOAD_FAILURE:
            sendData(parsed_pt);
            break;
        case UPLOAD_ACK:
            sendMessage(&close, false);
            doRead();
            break;
        case MESSAGE:
            (*parsed_pt).put("serverRecv", true);
            (*parsed_pt).put("task", MESSAGE_ACK);
            sendMessage(parsed_pt, false);
            doRead();
            break;
        case MESSAGE_ACK:
            doRead();
        case CLOSE:
            (*parsed_pt).put("isClient", false);
            (*parsed_pt).put("task", CLOSE_ACK);
            (*parsed_pt).put("serverRecv", true);
            sendMessage(parsed_pt, true);
            break;
        case CLOSE_ACK:
            doClose();
            break;
        default:
            doClose();
    }
}

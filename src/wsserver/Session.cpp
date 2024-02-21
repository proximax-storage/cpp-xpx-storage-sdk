#include "wsserver/Session.h"
#include "wsserver/Utils.h"
#include "wsserver/Task.h"
#include "wsserver/Base64.h"
#include "wsserver/EncryptDecrypt.h"
#include "wsserver/Message.h"
#include <iostream>
#include <filesystem>

#include <openssl/rand.h>
#include <openssl/evp.h>
#include <openssl/err.h>

namespace fs = std::filesystem;

std::string AES_P = "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD129024E088A67CC74020BBEA63B139B22514A08798E3404DDEF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7EDEE386BFB5A899FA5AE9F24117C4B1FE649286651ECE45B3DC2007CB8A163BF0598DA48361C55D39A69163FA8FD24CF5F83655D23DCA3AD961C62F356208552BB9ED529077096966D670C354E4ABC9804F1746C08CA18217C32905E462E36CE3BE39E772C180E86039B2783A2EC07A28FB5C55DF06F4C52C9DE2BCBF6955817183995497CEA956AE515D2261898FA051015728E5A8AACAA68FFFFFFFFFFFFFFFF";
std::string AES_G = "2";

void handleErrors() {
    std::cerr << "Error occurred.\n";
    ERR_print_errors_fp(stderr);
    exit(EXIT_FAILURE);
}

std::set<std::shared_ptr<Session>> Session::incoming_sessions;

void Session::run()
{
    // We need to be executing within a strand to perform async operations
    // on the I/O objects in this session. Although not strictly necessary
    // for single-threaded contexts, this example code is written to be
    // thread-safe by default.
    net::dispatch(ws.get_executor(),
        beast::bind_front_handler(
            &Session::onRun,
            shared_from_this()));
}

void Session::onRun()
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
            &Session::onAccept,
            shared_from_this()));
}

void Session::onAccept(beast::error_code ec)
{
    if(ec)
        return fail(ec, "accept");
    
    // Keep track of connected sessions
    incoming_sessions.insert(shared_from_this());

    std::cout << "Accepted Sessions:\n";
    for (const auto& sess : incoming_sessions) {
        std::cout << "Session ID: " << sess.get() << "\n";
    }

    ws.async_read(buffer,
        [this](beast::error_code ec, std::size_t bytes_transferred){
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
                    
                    int task = (parsed_pt).get<int>("task");
                    std::cout << "Sessions:\n";
                    for (const auto& sess : incoming_sessions) {
                        std::cout << "Session ID: " << sess.get() << "\n";
                    }

                    std::cout << "Current Session ID: " << shared_from_this().get() << std::endl;

                    if (task == Task::KEY_EX) {
                        std::cout << message << std::endl;
                        keyExchange(&parsed_pt);
                    } 
                    else {
                        doClose();
                    }
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
        });
}

void Session::doRead()
{
    // Read a message into our buffer
    ws.async_read(
        buffer,
        beast::bind_front_handler(
            &Session::onRead,
            shared_from_this()));
}

void Session::onRead(beast::error_code ec, std::size_t bytes_transferred)
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
        pt::ptree json;
        int isAuthentic = decodeMessage(boost::beast::buffers_to_string(buffer.data()), &json, sharedKey);
        buffer.consume(buffer.size());
        isAuthentic ? handleJson(&json) : doClose();
    }
}

void Session::onWrite(beast::error_code ec, std::size_t bytes_transferred)
{
    boost::ignore_unused(bytes_transferred);

    if(ec)
        fail(ec, "write");
}

void Session::onWriteClose(beast::error_code ec, std::size_t bytes_transferred)
{
    boost::ignore_unused(bytes_transferred);

    if(ec)
        fail(ec, "write");

    buffer.consume(buffer.size());
    doClose();
}

void Session::doClose()
{
    ws.async_close(websocket::close_code::normal,
        beast::bind_front_handler(
            &Session::onClose,
            shared_from_this()));
}

void Session::onClose(beast::error_code ec) 
{
    if (ec) {
        fail(ec, "close");
    } else {
        std::cout << "Session ID: " << shared_from_this().get() << " is closed successfully." << std::endl;
    }
    incoming_sessions.erase(shared_from_this());
}

void Session::keyExchange(pt::ptree* json) 
{
    OpenSSL_add_all_algorithms();
    ERR_load_crypto_strings();

    DH *dh = DH_new();

    if (!dh) {
        handleErrors();
    }

    BIGNUM *p = BN_new();
    BIGNUM *g = BN_new();

    BN_hex2bn(&p, AES_P.c_str());
    BN_hex2bn(&g, AES_G.c_str());

    DH_set0_pqg(dh, p, nullptr, g);
    // Generate public and private keys for Server
    if (DH_generate_key(dh) != 1) {
        handleErrors();
    }

    // Serialize public key of Server to send to Bob
    const BIGNUM *serverPublicKey = BN_new();
    DH_get0_key(dh, &serverPublicKey, nullptr);
    char *serializedSeverPublicKey = BN_bn2hex(serverPublicKey);

    // Send serializedSeverPublicKey to connected Client
    pt::ptree key;
    key.put("task", Task::KEY_EX);
    key.put("publicKey", serializedSeverPublicKey);

    std::ostringstream key_stream;
    write_json(key_stream, key, false);

    std::cout << key_stream.str() << std::endl;

    ws.write(net::buffer(key_stream.str()));
    
    unsigned char *sharedSecret = nullptr;

    BIGNUM *deserializedClientPublicKey = BN_new();
    std::string clientPublicKeyStr = (*json).get<std::string>("publicKey");
    BN_hex2bn(&deserializedClientPublicKey, clientPublicKeyStr.c_str());

    // Allocate memory for sharedSecret
    sharedSecret = (unsigned char*)OPENSSL_malloc(DH_size(dh));
    if (sharedSecret == nullptr) {
        // Handle memory allocation failure
        handleErrors();
    }

    if (DH_compute_key(sharedSecret, deserializedClientPublicKey, dh) == -1) {
        handleErrors();
    }
    
    std::string derivedKey = "";
    for (int i = 0; i < DH_size(dh); ++i) {
        std::stringstream ss;
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(sharedSecret[i]);
        derivedKey += ss.str();
    }

    sharedKey = derivedKey.substr(0, 32);

    DH_free(dh);
    BN_free(deserializedClientPublicKey);
    OPENSSL_free(sharedSecret); // Free allocated memory
    ERR_free_strings();

    doRead();
}

// Send message to a client
void Session::sendMessage(pt::ptree* json, bool is_close) 
{
    ws.async_write(
        net::buffer(encodeMessage(json, sharedKey)),
        beast::bind_front_handler(
            is_close ? &Session::onWriteClose : &Session::onWrite,
            shared_from_this()));
}

void Session::recvData(pt::ptree* json)
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

void Session::recvDataChunk(pt::ptree* json) {
    std::string uid = (*json).get<std::string>("uid");

    std::cout << recv_dataCounter[uid] << std::endl;
    std::string binaryString = base64_decode((*json).get<std::string>("data"));     
    std::ofstream file(recv_directory[uid], std::ios::binary | std::ios::app);
    file << binaryString;
    file.close();

    if (recv_dataCounter[uid] + 1 == recv_numOfDataPieces[uid]) {
        pt::ptree done;
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

void Session::sendData(pt::ptree* json) {
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

    (*json).put("task", Task::DOWNLOAD_INFO);
    (*json).put("dataSize", std::filesystem::file_size(filePath));
    (*json).put("dataPieceSize", send_DataPieceSize);
    (*json).put("numOfDataPieces", send_numOfDataPieces[uid]);
    (*json).put("uid", uid);

    send_dataCounter[uid]  = 0;

    ws.async_write(net::buffer(encodeMessage(json, sharedKey)),
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

                    std::ofstream outputFile("out_" + uid + fileName, std::ios::binary);
                    outputFile.write(fileBuffer.data(), bytesRead);
                    outputFile.close();

                    std::ifstream outputFile2("out_" + uid + fileName, std::ios::binary);
                    std::ostringstream ss;
                    ss << outputFile2.rdbuf();
                    outputFile2.close();
                    std::string bin_data = base64_encode(ss.str());
                    
                    pt::ptree data;
                    data.put("task", Task::DOWNLOAD_DATA);
                    data.put("data", bin_data);
                    data.put("dataPieceNum", send_dataCounter[uid]);
                    data.put("uid", uid);
                    
                    ws.write(net::buffer(encodeMessage(&data, sharedKey)));
                    
                    send_dataCounter[uid]++;
                    fileBuffer.assign(send_DataPieceSize, 0);
                }
                
                try {
                    std::filesystem::remove("out_" + uid + fileName);
                } catch (const std::filesystem::filesystem_error& e) {
                    std::cerr << "Error deleting file: " << e.what() << std::endl;
                }

                doRead();

            } else {
                // Handle errors or connection closure
                std::cerr << "Error in sending data: " << ec.message() << std::endl;
                doClose();
            }
    });
}

void Session::sendDataAck(pt::ptree* json) {
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

void Session::deleteData(pt::ptree* json) {
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

    ws.async_write(net::buffer(encodeMessage(json, sharedKey)), 
        [this](beast::error_code ec, std::size_t bytes_transferred){
            doRead();
    });
};

// Broadcast message to all Session
void Session::broadcastToAll(pt::ptree* json, bool is_close) 
{
    for (const auto& session : incoming_sessions) {
        session->sendMessage(json, is_close);
    }
}

// Make a request to all Session
void Session::requestToAll(pt::ptree* json) 
{
    for (const auto& session : incoming_sessions) {
        session->sendMessage(json, false);
        session->doRead();
    }
}

// Handles JSON receive by doRead()
void Session::handleJson(pt::ptree* parsed_pt)
{  
    std::ostringstream json_output;
    std::ostringstream print;
    pt::ptree close;

    std::cout << "Sessions:\n";
    for (const auto& sess : incoming_sessions) {
        std::cout << "Session ID: " << sess.get() << "\n";
    }

    std::cout << "Current Session ID: " << shared_from_this().get() << std::endl;
    
    int task = (*parsed_pt).get<int>("task");
    switch (task) {
        case KEY_EX:
            write_json(print, *parsed_pt);
            std::cout << print.str() << std::endl;
            keyExchange(parsed_pt);
            break;
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
            // doRead();
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
            (*parsed_pt).put("task", MESSAGE_ACK);
            write_json(json_output, *parsed_pt, false);
            sendMessage(parsed_pt, false);
            doRead();
            break;
        case CLOSE:
            write_json(print, *parsed_pt);
            std::cout << print.str() << std::endl;
            close.put("task", CLOSE_ACK);
            sendMessage(&close, true);
            break;
        case CLOSE_ACK:
            doClose();
            break;
        default:
            doClose();
    }
    
    return;
}

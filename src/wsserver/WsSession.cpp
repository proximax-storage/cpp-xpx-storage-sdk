#include "wsserver/WsSession.h"
#include "wsserver/Message.h"
#include "crypto/Signer.h"
#include "crypto/Hashes.h"
#include "drive/log.h"
#include "libtorrent/create_torrent.hpp"

#include <iostream>
#include <filesystem>
#include <sys/file.h>
#include <sys/stat.h>

#include <openssl/evp.h>
#include <openssl/ossl_typ.h>
#include <openssl/ec.h>
#include <openssl/pem.h>

#include <boost/archive/iterators/remove_whitespace.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/uuid/string_generator.hpp>
#include <utility>


// TODO: fix Man-in-the-middle attack (verify server key, authenticator app, etc)
// TODO: add logic for receipts
// TODO: add possibility to download data from client for each replicator independently and direct()


namespace sirius::wsserver
{

	WsSession::WsSession(const boost::uuids::uuid& uuid,
                 const sirius::crypto::KeyPair& keyPair,
				 boost::asio::io_context& ioCtx,
                 boost::asio::ip::tcp::socket&& socket,
                 std::filesystem::path& storageDirectory,
                 std::function<void(boost::property_tree::ptree data, std::function<void(boost::property_tree::ptree fsTreeJson)> callback)> fsTreeHandler,
                 std::function<void(const boost::uuids::uuid& id)> remover)
	: m_storageDirectory(storageDirectory)
    , fsTreeHandler(fsTreeHandler)
	, removeSession(remover)
    , m_id(uuid)
    , m_uuidGenerator({})
	, m_networkStrand(ioCtx)
	, m_downloadsStrand(ioCtx)
    , m_ws(std::move(socket))
    , m_keyPair(keyPair)
{}

void WsSession::run()
{
    std::error_code ec;
    std::filesystem::create_directories(m_storageDirectory, ec);
    if (ec)
    {
        __LOG_WARN("Session::run: create directories error: " << m_storageDirectory.string() << " error: " << ec.message())
        // TODO: send error back
        return;
    }

	boost::asio::dispatch(m_networkStrand, [pThis = shared_from_this()]()
    {
        pThis->m_ws.set_option(boost::beast::websocket::stream_base::timeout::suggested(boost::beast::role_type::server));
        pThis->m_ws.set_option(boost::beast::websocket::stream_base::decorator([](boost::beast::websocket::response_type& res)
        {
            res.set(boost::beast::http::field::server, std::string(BOOST_BEAST_VERSION_STRING) + " websocket-server-async");
        }));

        pThis->m_ws.async_accept([pThis](auto ec)
        {
            pThis->onAccept(ec);
        });
    });
}

void WsSession::onAccept(boost::beast::error_code ec)
{
    boost::asio::post(m_networkStrand, [pThis = shared_from_this(), ec]()
    {
        if (ec)
        {
            __LOG_WARN( "Session::onAccept: " << ec.message() )
            pThis->removeSession(pThis->m_id);
            return;
        }

        auto clientEndpoint = pThis->m_ws.next_layer().socket().remote_endpoint();
        auto clientIp = clientEndpoint.address().to_string();
        auto clientPort = clientEndpoint.port();

        __LOG( "Session::onAccept: Session id: " << to_string(pThis->m_id) << " client endpoint: " << clientIp << " : " << clientPort << " storage directory: " << pThis->m_storageDirectory)
        pThis->m_ws.async_read(pThis->m_buffer, [pThis](boost::beast::error_code ec, std::size_t)
        {
            if (ec == boost::beast::websocket::error::closed)
            {
                __LOG( "Session::onAccept:closed: " << to_string(pThis->m_id) )
                pThis->removeSession(pThis->m_id);
                return;
            }

            if (ec)
            {
                __LOG( "Session::onAccept:async_read: " << to_string(pThis->m_id) << " message: " << ec.message() )
                pThis->doClose();
                pThis->removeSession(pThis->m_id);
            }
            else
            {
                const std::string message = boost::beast::buffers_to_string(pThis->m_buffer.data());
                pThis->m_buffer.consume(pThis->m_buffer.size());

                try
                {
                    pThis->keyExchange(stringToJson(message));
                } catch (const boost::property_tree::json_parser_error& e)
                {
                    __LOG_WARN( "Session::onAccept:async_read:: JSON parsing error: " << e.what() )
                    pThis->doClose(JSON_PARSER_ERROR, e.what());
                    pThis->removeSession(pThis->m_id);
                } catch (const std::exception& e)
                {
                    __LOG_WARN( "Session::onAccept:async_read:: An unexpected error occurred: " << e.what() )
                    pThis->doClose(UNEXPECTED_INTERNAL_ERROR, e.what());
                    pThis->removeSession(pThis->m_id);
                } catch (...)
                {
                    __LOG_WARN( "Session::onAccept:async_read:: An unexpected and unknown error occurred." )
                    pThis->doClose(UNEXPECTED_INTERNAL_ERROR, "An unexpected and unknown error occurred.");
                    pThis->removeSession(pThis->m_id);
                }
            }
        });
    });
}

void WsSession::doRead()
{
    boost::asio::post(m_networkStrand, [pThis = shared_from_this()]()
    {
        pThis->m_ws.async_read(pThis->m_buffer, [pThis](auto ec, auto bytesTransferred)
        {
            pThis->onRead(ec, bytesTransferred);
        });
    });
}

void WsSession::onRead(boost::beast::error_code ec, std::size_t)
{
	if (ec == boost::beast::websocket::error::closed || ec == boost::asio::stream_errc::eof || ec == boost::asio::error::not_connected)
	{
		__LOG( "Session::onRead: connection closed by client: " << to_string(m_id) )
		removeSession(m_id);
        return;
	}

    if (ec == boost::asio::error::operation_aborted)
    {
        __LOG( "Session::onRead: operation_aborted: " << to_string(m_id)  << " message: " << ec.message() )
        return;
    }

	if (ec)
	{
		__LOG_WARN( "Session::onRead::error: " << to_string(m_id) << " message: " << ec.message() )
        doClose(READING_MESSAGE_ERROR, ec.message());
        removeSession(m_id);
        return;
	}
	else
	{
        const auto buffer = boost::beast::buffers_to_string(m_buffer.data());
        auto pTree = stringToJson(buffer);
        if (pTree->empty())
        {
            __LOG_WARN( "Session::onRead::error: session id: " << to_string(m_id) << " message: buffer is empty " )
            doClose(RECEIVED_EMPTY_DATA, "buffer is empty");
            removeSession(m_id);
            return;
        }

        const auto payload = pTree->get_optional<std::string>("payload");
        if (!payload.has_value() || payload.value().empty())
        {
            __LOG_WARN( "Session::onRead::error: session id: " << to_string(m_id) << " message: payload is empty " )
            doClose(RECEIVED_INVALID_DATA, "payload is empty");
            removeSession(m_id);
            return;
        }

        const auto tag = pTree->get_optional<std::string>("metadata.tag");
        if (!tag.has_value() || tag.value().empty())
        {
            __LOG_WARN( "Session::onRead::error: session id: " << to_string(m_id) << " message: tag is empty " )
            doClose(RECEIVED_INVALID_DATA, "tag is empty");
            removeSession(m_id);
            return;
        }

        const auto iv = pTree->get_optional<std::string>("metadata.iv");
        if (!iv.has_value() || iv.value().empty())
        {
            __LOG_WARN( "Session::onRead::error: session id: " << to_string(m_id) << " message: iv is empty " )
            doClose(RECEIVED_INVALID_DATA, "iv is empty");
            removeSession(m_id);
            return;
        }

        m_buffer.consume(m_buffer.size());

        const auto decodedPayload = base64_decode(payload.value());
        const auto decodedIv = base64_decode(iv.value());
        const auto decodedTag = base64_decode(tag.value());
        const auto decryptedPayload = decrypt(m_sharedKey, decodedPayload, decodedIv, decodedTag);
        if (decryptedPayload.empty())
        {
            __LOG_WARN( "Session::onRead::error: session id: " << to_string(m_id) << " message: decrypted data is empty " )
            doClose(DECRYPTION_ERROR, "decrypted data is empty");
            removeSession(m_id);
            return;
        }

        auto payloadTree = stringToJson(decryptedPayload);
        if (payloadTree->empty())
        {
            __LOG_WARN( "Session::onRead::error: session id: " << to_string(m_id) << " message: payload tree is empty " )
            doClose(RECEIVED_INVALID_DATA, "payload tree is empty");
            removeSession(m_id);
            return;
        }
        else
        {
            handlePayload(payloadTree);
            doRead();
        }
	}
}

void WsSession::onWrite(boost::beast::error_code ec, std::size_t bytes_transferred)
{
	boost::ignore_unused(bytes_transferred);
	if (ec)
	{
		__LOG_WARN( "Session::onWrite::error: " << to_string(m_id) << " message: " << ec.message() )
	}
}

void WsSession::doClose()
{
    boost::asio::post(m_networkStrand, [pThis = shared_from_this()]()
    {
        pThis->m_ws.async_close(boost::beast::websocket::close_code::normal, [pThis](auto ec)
        {
            pThis->onClose(ec);
        });
    });
}

void WsSession::doClose(ServerErrorCode code, const std::string& message)
{
    auto closeMessage = generateBasicPayload(generateMessageId(), Type::FAILURE);
    closeMessage->put("message", message);
    closeMessage->put("code", static_cast<int>(code));

    const auto closeMessageStr = jsonToString(closeMessage);
    if (closeMessageStr.empty())
    {
        __LOG_WARN( "Session::doClose: session id: " << to_string(m_id) << " message: json is empty " )
        doClose();
        removeSession(m_id);
        return;
    }

    const EncryptionResult encryptedPayload = encrypt(m_sharedKey, closeMessageStr);
    if (encryptedPayload.cipherData.empty())
    {
        __LOG_WARN("Session::doClose: payload encryption error")
        doClose();
        removeSession(m_id);
        return;
    }

    auto finalMessage = generateFinalMessage(encryptedPayload);
    sendMessage(finalMessage);
    doClose();
}

void WsSession::onClose(boost::beast::error_code ec)
{
	if (ec)
	{
		__LOG_WARN( "Session::onClose::error: " << to_string(m_id) << " message: " << ec.message() )
	}
	else
	{
		__LOG( "Session::onClose::Session ID: " << to_string(m_id) << " is closed successfully." )
	}
}

void WsSession::keyExchange(std::shared_ptr<boost::property_tree::ptree> json)
{
    const auto inboundPayload = json->get_optional<std::string>("payload");
    if (!inboundPayload.has_value() || inboundPayload.value().empty())
    {
        __LOG_WARN( "Session::keyExchange: " << to_string(m_id) << " message: missed payload" )
        doClose();
        removeSession(m_id);
        return;
    }

    auto signature = json->get_optional<std::string>("metadata.signature");
    if (!signature.has_value() || signature.value().empty())
    {
        __LOG_WARN( "Session::keyExchange: " << to_string(m_id) << " message: missed signature" )
        doClose();
        removeSession(m_id);
        return;
    }

    auto inboundPayloadTree = stringToJson(inboundPayload.value());
    if (inboundPayloadTree->empty())
    {
        __LOG_WARN( "Session::keyExchange: " << to_string(m_id) << " message: payload tree is empty" )
        doClose();
        removeSession(m_id);
        return;
    }

    auto id = inboundPayloadTree->get_optional<std::string>("id");
    if (!id.has_value() || !isValidUUIDv4(id.value()))
    {
        __LOG_WARN( "Session::keyExchange: " << to_string(m_id) << " message: invalid message id format" )
        doClose(RECEIVED_INVALID_DATA, "invalid message id format");
        removeSession(m_id);
        return;
    }

    auto type = inboundPayloadTree->get_optional<int>("type");
    if (!type.has_value() || type.value() != Type::HANDSHAKE_REQUEST)
    {
        __LOG_WARN( "Session::keyExchange: " << to_string(m_id) << " message: invalid type id" )
        doClose();
        removeSession(m_id);
        return;
    }

    auto clientPublicKey = inboundPayloadTree->get_optional<std::string>("clientPublicKey");
    if (!clientPublicKey.has_value() || clientPublicKey.value().empty())
    {
        __LOG_WARN( "Session::keyExchange: " << to_string(m_id) << " message: missed client public key" )
        doClose();
        removeSession(m_id);
        return;
    }

    m_clientPublicKey = clientPublicKey.value();

    bool isVerified = verify( m_clientPublicKey, inboundPayload.value(), signature.value() );
    if (!isVerified)
    {
        __LOG_WARN( "Session::keyExchange: " << to_string(m_id) << " message: verification data error" )
        doClose();
        removeSession(m_id);
        return;
    }

    auto clientSessionPublicKey = inboundPayloadTree->get_optional<std::string>("sessionPublicKey");
    if (!clientSessionPublicKey.has_value() || clientSessionPublicKey.value().empty())
    {
        __LOG_WARN( "Session::keyExchange: " << to_string(m_id) << " message: invalid client session public key" )
        doClose();
        removeSession(m_id);
        return;
    }

    std::string serverSessionPublicKey;
    generateSessionKeyPair(clientSessionPublicKey.value(), serverSessionPublicKey);

    auto payloadOutboundTree = generateBasicPayload(id.value(), Type::HANDSHAKE_RESPONSE);
    const EncryptionResult encryptedSessionId = encrypt(m_sharedKey, to_string(m_id));
    if (encryptedSessionId.cipherData.empty())
    {
        __LOG_WARN("Session::keyExchange: encryption of the session id error")
        doClose();
        removeSession(m_id);
        return;
    }

    payloadOutboundTree->put("sessionId",  base64_encode(encryptedSessionId.cipherData));
    payloadOutboundTree->put("serverPublicKey", base64_encode(m_keyPair.publicKey().data(), m_keyPair.publicKey().size()));
    payloadOutboundTree->put("sessionPublicKey",  serverSessionPublicKey);

    const auto payloadOutboundStr = jsonToString(payloadOutboundTree);
    if (payloadOutboundStr.empty())
    {
        __LOG_WARN( "Session::keyExchange: session id: " << to_string(m_id) << " message: outbound payload is empty" )
        doClose();
        removeSession(m_id);
        return;
    }

    sirius::Signature payloadSignature;
    sign(m_keyPair, payloadOutboundStr, payloadSignature);

    auto outboundMessage = std::make_shared<boost::property_tree::ptree>();
    outboundMessage->put("payload", payloadOutboundStr);
    outboundMessage->put("metadata.iv", base64_encode(encryptedSessionId.iv));
    outboundMessage->put("metadata.tag", base64_encode(encryptedSessionId.tag));
    outboundMessage->put("metadata.signature", base64_encode(payloadSignature.data(), payloadSignature.size()));

    const auto finalOutboundMessage = jsonToString(outboundMessage);
    if (finalOutboundMessage.empty())
    {
        __LOG_WARN( "Session::keyExchange: " << to_string(m_id) << " message: final message is empty" )
        doClose();
        removeSession(m_id);
        return;
    }

    boost::asio::post(m_networkStrand, [pThis = shared_from_this(), finalOutboundMessage]()
    {
        pThis->m_ws.async_write(boost::asio::buffer(finalOutboundMessage), [pThis](auto ec, auto bytesTransferred)
        {
            pThis->onWrite(ec, bytesTransferred);
        });
    });

    doRead();
}

void WsSession::sendMessage(std::shared_ptr<boost::property_tree::ptree> json)
{
    boost::asio::post(m_networkStrand, [pThis = shared_from_this(), json]()
    {
        const auto data = jsonToString(json);
        if (data.empty())
        {
            __LOG_WARN( "Session::sendMessage: session id: " << to_string(pThis->m_id) << " message: data is empty!" )
            return;
        }

        const auto buffer = boost::asio::buffer(data);
        pThis->m_ws.async_write(buffer, [pThis](auto ec, auto bytesTransferred)
        {
            pThis->onWrite(ec, bytesTransferred);
        });
    });
}

void WsSession::recvData(std::shared_ptr<boost::property_tree::ptree> json)
{
	auto uid = json->get<std::string>("uid");
	m_recvDirectory[uid] = "saved-data/" + json->get<std::string>("drive") + json->get<std::string>("directory");

	try
	{
		std::filesystem::create_directories(m_recvDirectory[uid]);
		std::cout << "Folder created successfully: " << m_recvDirectory[uid] << std::endl;
	} catch (const std::filesystem::filesystem_error& ex)
	{
		std::cerr << "Error creating folder: " << ex.what() << std::endl;
	}

	m_recvDirectory[uid] += json->get<std::string>("fileName");

	// Check if the file exists
	if (std::filesystem::exists(m_recvDirectory[uid]))
	{
		// Delete the existing file
		if (std::filesystem::remove(m_recvDirectory[uid]))
		{
			std::cout << "Existing file deleted for upload: " << m_recvDirectory[uid] << std::endl;
		}
		else
		{
			std::cerr << "Error deleting existing file for upload: " << m_recvDirectory[uid] << std::endl;
			return;
		}
	}

	m_recvNumOfDataPieces[uid] = json->get<int>("numOfDataPieces");
	m_recvDataCounter[uid] = 0;

	//doRead();
}

void WsSession::recvDataChunk(std::shared_ptr<boost::property_tree::ptree> json)
{
	auto uid = json->get<std::string>("uid");

	std::cout << m_recvDataCounter[uid] << std::endl;
	std::string binaryString = base64_decode(json->get<std::string>("data"));
	std::ofstream file(m_recvDirectory[uid], std::ios::binary | std::ios::app);
	file << binaryString;

	// Check if any error occurred during the appending process
	if (file.fail())
	{
        __LOG_WARN( " Session::recvDataChunk: Error appending data to file: " << json->get<std::string>("fileName") )
		json->put("type", Type::SERVER_DOWNLOAD_DATA_FAILURE);
        // TODO: encrypt data before sending
        sendMessage(json);
        //doRead();

        return;
	}

	if (m_recvDataCounter[uid] + 1 == m_recvNumOfDataPieces[uid])
	{
		auto pTree = std::make_shared<boost::property_tree::ptree>();
		//pTree->put("type", Type::UPLOAD_ACK);
		pTree->put("uid", uid);

        // TODO: encrypt data before sending
		sendMessage(pTree);
		std::cout << "Successfully uploaded to server: " << m_recvDirectory[uid] << std::endl;

		m_recvDirectory[uid] = "";
		m_recvDataCounter[uid] = 0;

		auto it = m_recvDirectory.find(uid);
		auto it2 = m_recvDataCounter.find(uid);
		auto it3 = m_recvNumOfDataPieces.find(uid);

		if (it != m_recvDirectory.end() && it2 != m_recvDataCounter.end() && it3 != m_recvNumOfDataPieces.end())
		{
			m_recvDirectory.erase(it);
			m_recvDataCounter.erase(it2);
			m_recvNumOfDataPieces.erase(it3);
			std::cout << "Upload uid: " << uid << " has been deleted" << std::endl;
		}
		else
		{
			std::cerr << "Error deleting upload uid: " << uid << std::endl;
		}
	}
	else
	{
		m_recvDataCounter[uid]++;
	}

	//doRead();
}

void WsSession::sendData(std::shared_ptr<boost::property_tree::ptree> json)
{
	auto fileName = json->get<std::string>("fileName");
	std::string filePath = "get-data/" + json->get<std::string>("drive") + json->get<std::string>("directory") + fileName;
	auto uid = json->get<std::string>("uid");

	if (!(std::filesystem::exists(filePath)))
	{
        __LOG_WARN( "Session::sendData:: file does not exist: " << filePath )
        // TODO: encrypt data before sending
        sendMessage(json);
        //doRead();
        return;
	}

    boost::asio::post(m_networkStrand, [pThis = shared_from_this(), json, uid, filePath, fileName]()
    {
        pThis->m_sendNumOfDataPieces[uid] = std::filesystem::file_size(filePath) / pThis->m_sendDataPieceSize;
        if (std::filesystem::file_size(filePath) % pThis->m_sendDataPieceSize != 0)
        {
            pThis->m_sendNumOfDataPieces[uid]++;
        }

        //json->put("type", Type::DOWNLOAD_INFO);
        json->put("dataSize", std::filesystem::file_size(filePath));
        json->put("dataPieceSize", pThis->m_sendDataPieceSize);
        json->put("numOfDataPieces", pThis->m_sendNumOfDataPieces[uid]);
        json->put("uid", uid);

        pThis->m_sendDataCounter[uid] = 0;
//        const auto buffer = boost::asio::buffer(encodeMessage(json, pThis->m_sharedKey));
//        pThis->m_ws.async_write(buffer,[pThis, uid, filePath, fileName](auto ec, auto bytes_transferred)
//        {
//            if (!ec)
//            {
//                std::vector<char> fileBuffer(pThis->m_sendDataPieceSize, 0);
//                std::ifstream file;
//                file.open(filePath, std::ios::binary);
//                if (!file.is_open())
//                {
//                    std::cerr << "[Download info] Failed to open file: " << filePath << std::endl;
//                    return;
//                }
//
//                if (std::filesystem::exists("out_" + uid + fileName))
//                {
//                    // Delete the file
//                    if (std::filesystem::remove("out_" + uid + fileName))
//                    {
//                        std::cout << "File deleted successfully." << std::endl;
//                    }
//                }
//
//                while (!file.eof())
//                {
//                    file.read(fileBuffer.data(), fileBuffer.size());
//                    std::streamsize bytesRead = file.gcount();
//
//                    std::ofstream outputFile("out_" + uid + fileName, std::ios::binary);
//                    outputFile.write(fileBuffer.data(), bytesRead);
//                    outputFile.close();
//
//                    std::ifstream outputFile2("out_" + uid + fileName, std::ios::binary);
//                    std::ostringstream ss;
//                    ss << outputFile2.rdbuf();
//                    outputFile2.close();
//                    std::string bin_data = base64_encode(ss.str());
//
//                    auto pData = std::make_shared<boost::property_tree::ptree>();
//                    pData->put("task", Task::DOWNLOAD_DATA);
//                    pData->put("data", bin_data);
//                    pData->put("dataPieceNum", pThis->m_sendDataCounter[uid]);
//                    pData->put("uid", uid);
//
//                    // TODO: encrypt data before sending
//                    pThis->sendMessage(pData);
//                    pThis->m_sendDataCounter[uid]++;
//                    fileBuffer.assign(pThis->m_sendDataPieceSize, 0);
//                }
//
//                try
//                {
//                    // TODO: use error code instead of exceptions
//                    std::filesystem::remove("out_" + uid + fileName);
//                } catch (const std::filesystem::filesystem_error& e)
//                {
//                    __LOG_WARN( "Session::sendData::Error deleting file:" << e.what() )
//                }
//
//                //pThis->doRead();
//            }
//            else
//            {
//                // Handle errors or connection closure
//                __LOG_WARN( "Session::sendData::Error in sending data:" << ec.message() )
//                pThis->doClose();
//            }
//        });
    });
}

void WsSession::sendDataAck(std::shared_ptr<boost::property_tree::ptree> json)
{
	const auto uid = json->get<std::string>("uid");
	auto it = m_sendNumOfDataPieces.find(uid);
	auto it2 = m_sendDataCounter.find(uid);

	if (it != m_sendNumOfDataPieces.end() && it2 != m_sendDataCounter.end())
	{
		m_sendNumOfDataPieces.erase(it);
		m_sendDataCounter.erase(it2);
		std::cout << "Download uid: " << uid << " has been deleted" << std::endl;
	}
	else
	{
		std::cerr << "Error deleting download uid: " << uid << std::endl;
	}
}

void WsSession::deleteData(std::shared_ptr<boost::property_tree::ptree> json)
{
	std::string deleteFilePath = "saved-data/" + json->get<std::string>("drive") +
								 json->get<std::string>("directory") + json->get<std::string>("fileName");

	// Check if the file exists
	if (std::filesystem::exists(deleteFilePath))
	{
		// Delete the existing file
		if (std::filesystem::remove(deleteFilePath))
		{
			std::cout << "File deleted: " << deleteFilePath << std::endl;
			json->put("type", Type::SERVER_DELETE_DATA_ACK);
		}
		else
		{
			json->put("type", Type::SERVER_DELETE_DATA_FAILURE);
			std::cerr << "Error deleting file: " << deleteFilePath << std::endl;
		}
	}
	else
	{
		std::cout << "File does not exist: " << deleteFilePath << std::endl;
		json->put("type", Type::SERVER_DELETE_DATA_FAILURE);
	}

    // TODO: encrypt data before sending
    sendMessage(json);
    //doRead();
};

void WsSession::broadcastToAll(std::shared_ptr<boost::property_tree::ptree> json)
{
//	for (const auto& session : incoming_sessions)
//	{
//		session->sendMessage(json);
//	}
}

void WsSession::requestToAll(std::shared_ptr<boost::property_tree::ptree> json)
{
//	for (const auto& session : incoming_sessions)
//	{
//		session->sendMessage(json);
//		session->doRead();
//	}
}

void WsSession::sendChunk(const uint64_t chunkIndex,
						const std::string& driveKey,
						const std::string& fileHash,
						const std::string& chunkHash,
						const std::string& chunk)
{
	const auto responseId = generateMessageId();
	auto payloadOutboundTree = generateBasicPayload(responseId, Type::CLIENT_DOWNLOAD_PIECE_RESPONSE);
	payloadOutboundTree->put("chunkIndex", chunkIndex);
	payloadOutboundTree->put("chunkHash", chunkHash);
	payloadOutboundTree->put("hash", fileHash);
	payloadOutboundTree->put("driveKey", driveKey);
	payloadOutboundTree->put("data", chunk);

	const auto payloadStr = jsonToString(payloadOutboundTree);
	if (payloadStr.empty())
	{
		__LOG_WARN("Session::sendChunk: json conversion error: " << responseId)
		return;
	}

	const EncryptionResult encryptedResponse = encrypt(m_sharedKey, payloadStr);
	if (encryptedResponse.cipherData.empty())
	{
		__LOG_WARN("Session::sendChunk: encryption error " << responseId)
		return;
	}

	auto finalMessage = generateFinalMessage(encryptedResponse);
	sendMessage(finalMessage);
}

void WsSession::sendFileDescription(const std::string& responseId,
								  const std::string& driveKey,
								  const std::string& fileHash,
								  int chunkSize,
								  uint64_t fileSize)
{
	auto payloadOutboundTree = generateBasicPayload(responseId, Type::CLIENT_DOWNLOAD_FILES_RESPONSE);
	payloadOutboundTree->put("size", fileSize);
	payloadOutboundTree->put("chunkSize", chunkSize);
	payloadOutboundTree->put("chunksAmount", static_cast<uint64_t>(chunkSize) >= fileSize ? 1 : std::ceil(fileSize/chunkSize));
	payloadOutboundTree->put("driveKey", driveKey);
	payloadOutboundTree->put("hash", fileHash);

	const auto payloadStr = jsonToString(payloadOutboundTree);
	if (payloadStr.empty())
	{
		__LOG_WARN("Session::sendFileDescription: json conversion error: " << responseId)
		return;
	}

	const EncryptionResult encryptedResponse = encrypt(m_sharedKey, payloadStr);
	if (encryptedResponse.cipherData.empty())
	{
		__LOG_WARN("Session::sendFileDescription: encryption error " << responseId)
		return;
	}

	auto finalMessage = generateFinalMessage(encryptedResponse);
	sendMessage(finalMessage);
}

void WsSession::handleUploadDataStartRequest(std::shared_ptr<boost::property_tree::ptree> json)
{
    __LOG( "Session::handleUploadDataStartRequest:::Session ID: " << to_string(m_id) )
    if (!validateUploadDataStartRequest(json))
    {
        __LOG_WARN("Session::handleUploadDataStartRequest: json validation error: " << jsonToString(json))
        return;
    }

    // TODO: check file already exists on the drive
    // TODO: use hash as a key
    const auto fileHash = json->get<std::string>("hash");
    if (m_downloads.contains(fileHash))
    {
        __LOG_WARN("Session::handleUploadDataStartRequest: file already exists in downloads cache (skipping): request id: "
        << json->get<std::string>("id")
        << " file hash: "
        << fileHash
        << " file name: "
        << json->get<std::string>("name"))

        return;
    }
    else
    {
        FileDescriptor newFileDescriptor;
        newFileDescriptor.m_name = json->get<std::string>("name");
        newFileDescriptor.m_hash = json->get<std::string>("hash"); // TODO: use hash
        newFileDescriptor.m_size = json->get<uint64_t>("size");
        newFileDescriptor.m_chunksAmount = json->get<uint64_t>("chunksAmount");
        newFileDescriptor.m_chunkSize = json->get<unsigned int>("chunkSize");
        newFileDescriptor.m_driveKey = json->get<std::string>("driveKey");

        newFileDescriptor.m_relativePath = std::filesystem::path(newFileDescriptor.m_driveKey);
        newFileDescriptor.m_path = std::filesystem::path(json->get<std::string>("path"));
        if (newFileDescriptor.m_path.empty())
        {
            newFileDescriptor.m_relativePath /= std::filesystem::path(fileHash);
        }
        else
        {
            newFileDescriptor.m_relativePath /= newFileDescriptor.m_path / std::filesystem::path(fileHash);
        }

        m_downloads.insert( { newFileDescriptor.m_hash, newFileDescriptor } );

        std::error_code ec;
        std::filesystem::create_directories(newFileDescriptor.m_relativePath.parent_path(), ec);
        if (ec)
        {
            __LOG_WARN("Session::handleUploadDataStartRequest: create directories error: " << newFileDescriptor.m_relativePath.string() << " error: " << ec.message())
            // TODO: send error back
            return;
        }

        std::filesystem::path currentPath = std::filesystem::current_path();
        std::filesystem::path absolutePath = currentPath / newFileDescriptor.m_relativePath;
        m_downloads[newFileDescriptor.m_hash].m_stream = std::make_unique<std::ofstream>(absolutePath, std::ios::binary | std::ios::app);
        if(!m_downloads[newFileDescriptor.m_hash].m_stream->is_open())
        {
            __LOG_WARN("Session::handleUploadDataStartRequest: could not open file: " << absolutePath.string() << " hash: " << fileHash)
            // TODO: send error back
            return;
        }
    }

    const auto responseId = json->get<std::string>("id");
    auto payloadOutboundTree = generateBasicPayload(responseId, Type::SERVER_READY_RESPONSE);
    const auto payloadStr = jsonToString(payloadOutboundTree);
    if (payloadStr.empty())
    {
        __LOG_WARN("Session::handleUploadDataStartRequest: SERVER_READY_RESPONSE json conversion error: " << responseId)
        return;
    }

    const EncryptionResult encryptedAck = encrypt(m_sharedKey, payloadStr);
    if (encryptedAck.cipherData.empty())
    {
        __LOG_WARN("Session::handleUploadDataStartRequest: encryption of the SERVER_READY_RESPONSE error " << responseId)
        return;
    }

    auto finalMessage = generateFinalMessage(encryptedAck);
    sendMessage(finalMessage);
}

void WsSession::handleUploadDataRequest(std::shared_ptr<boost::property_tree::ptree> json)
{
    __LOG( "Session::handleUploadDataRequest:::Session ID: " << to_string(m_id) )
    if (!validateUploadDataRequest(json))
    {
        __LOG_WARN("Session::handleUploadDataRequest: json validation error: " << jsonToString(json))
        return;
    }

    const auto fileHash = json->get<std::string>("hash");
    if (!m_downloads.contains(fileHash))
    {
        __LOG_WARN("Session::handleUploadDataRequest: file not found: " << fileHash)
        return;
    }

    if (!m_downloads[fileHash].m_stream->is_open())
    {
        __LOG_WARN("Session::handleUploadDataRequest: could not open the file: " << fileHash)
        // TODO: send failure back and remove file from downloads
        return;
    }

    *m_downloads[fileHash].m_stream << base64_decode(json->get<std::string>("data"));
    if (m_downloads[fileHash].m_stream->fail())
    {
        // TODO: send failure back and remove file from downloads
        __LOG_WARN("Session::handleUploadDataRequest: saving chunk failure: chunk hash:" << json->get<std::string>("chunkHash"))
        return;
    }

    const auto chunkIndex = json->get<std::uint64_t>("chunkIndex") + 1;
    if (m_downloads[fileHash].m_chunksAmount == chunkIndex)
    {
        m_downloads[fileHash].m_stream->close();
        // TODO: add rename and check file.

        std::filesystem::path currentPath = std::filesystem::current_path();
        std::filesystem::path absolutePath = currentPath / m_downloads[fileHash].m_relativePath;
        __LOG("Session::handleUploadDataRequest: file path: " << fileHash << " path:" << absolutePath.string())
    }

    const auto responseId = json->get<std::string>("id");
    auto payloadOutboundTree = generateBasicPayload(responseId, Type::SERVER_ACK);
    const auto payloadStr = jsonToString(payloadOutboundTree);
    if (payloadStr.empty())
    {
        __LOG_WARN("Session::handleUploadDataRequest: SERVER_ACK json conversion error: " << responseId)
        return;
    }

    const EncryptionResult encryptedAck = encrypt(m_sharedKey, payloadStr);
    if (encryptedAck.cipherData.empty())
    {
        __LOG_WARN("Session::handleUploadDataRequest: encryption of the SERVER_ACK error " << responseId)
        return;
    }

    auto finalMessage = generateFinalMessage(encryptedAck);
    sendMessage(finalMessage);
}

long WsSession::getCurrentTimestamp()
{
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
}

std::string WsSession::generateMessageId()
{
    const auto messageId = m_uuidGenerator();
    return to_string(messageId);
}

bool WsSession::isValidUUIDv4(const std::string& uuid)
{
    bool result = false;
    try
    {
        boost::uuids::string_generator generator;
        boost::uuids::uuid id = generator(uuid);

        if (id.version() == boost::uuids::uuid::version_type::version_random_number_based)
        {
            const auto formattedUuid = boost::uuids::to_string(id);
            if (boost::iequals(formattedUuid, uuid))
            {
                result = true;
            }
        }
    } catch (const std::exception& e)
    {
        result = false;
    }

    return result;
}

bool WsSession::validateUploadDataStartRequest(std::shared_ptr<boost::property_tree::ptree> json)
{
    // TODO: add all fields and check value
    return  json->get_optional<uint64_t>("size") &&
            json->get_optional<unsigned int>("chunkSize") &&
            json->get_optional<uint64_t>("chunksAmount") &&
            json->get_optional<std::string>("name");
}

bool WsSession::validateUploadDataRequest(std::shared_ptr<boost::property_tree::ptree> json)
{
    // TODO: add all fields and check value
    return  json->get_optional<uint64_t>("chunkIndex") &&
            json->get_optional<std::string>("chunkHash") &&
            json->get_optional<std::string>("hash") &&
            json->get_optional<std::string>("data");
}

void WsSession::handlePayload(std::shared_ptr<boost::property_tree::ptree> json)
{
	__LOG( "Session::handleJson:::Session ID: " << to_string(m_id) )

	auto type = json->get_optional<int>("type");
	if (!type.has_value())
	{
		__LOG_WARN( "Session::handleJson::type error: " << to_string(m_id) << " data: " << json )
		return;
	}

	switch (type.value())
	{
		case Type::HANDSHAKE_REQUEST:
		{
			keyExchange(json);
			break;
		}

        case CLIENT_UPLOAD_DATA_START_REQUEST:
        {
            boost::asio::post(m_downloadsStrand, [pThis = shared_from_this(), json]()
            {
                pThis->handleUploadDataStartRequest(json);
            });
            break;
        }

        case CLIENT_UPLOAD_DATA_REQUEST:
        {
            boost::asio::post(m_downloadsStrand, [pThis = shared_from_this(), json]()
            {
                pThis->handleUploadDataRequest(json);
            });
            break;
        }

        case FS_TREE_REQUEST:
        {
			auto data = json->get_optional<std::string>("data");
			if (!data.has_value())
			{
				__LOG_WARN( "FS_TREE_REQUEST: " << to_string(m_id) << " data is empty!" )
				// TODO: send error to client
				return;
			}

            if (!fsTreeHandler)
            {
                __LOG_WARN( "FS_TREE_REQUEST: " << to_string(m_id) << " data: " << data.value() << " callback is not set!" )
                // TODO: send error to client
                return;
            }

			const auto responseId = json->get<std::string>("id");
            auto callback = [pThis = shared_from_this(), responseId](boost::property_tree::ptree fsTree)
            {
				auto fsTreePtr = std::make_shared<boost::property_tree::ptree>(fsTree);
				const auto fsTreeJson = jsonToString(fsTreePtr);
                __LOG( "Session::handlePayload FS_TREE_REQUEST callback, fsTree: " << to_string(pThis->m_id) << " data: " << fsTreeJson )

				auto payloadOutboundTree = pThis->generateBasicPayload(responseId, Type::FS_TREE_RESPONSE);
				payloadOutboundTree->put("data", fsTreeJson);
				const auto payloadStr = jsonToString(payloadOutboundTree);
				if (payloadStr.empty())
				{
					__LOG_WARN("Session::handlePayload FS_TREE_RESPONSE callback: json conversion error: " << responseId)
					return;
				}

				const EncryptionResult encryptedResponse = encrypt(pThis->m_sharedKey, payloadStr);
				if (encryptedResponse.cipherData.empty())
				{
					__LOG_WARN("Session::handlePayload: encryption of the FS_TREE_RESPONSE error " << responseId)
					return;
				}

				auto finalMessage = pThis->generateFinalMessage(encryptedResponse);
				pThis->sendMessage(finalMessage);
            };

			auto jsonData = stringToJson(data.value());
            fsTreeHandler(*jsonData, callback);

            break;
        }

		case CLIENT_DOWNLOAD_FILES_REQUEST:
		{
			auto data = json->get_optional<std::string>("data");
			if (!data.has_value())
			{
				__LOG_WARN( "CLIENT_DOWNLOAD_FILES_REQUEST: " << to_string(m_id) << " data is empty!" )
				// TODO: send error to client
				return;
			}

			auto jsonData = stringToJson(data.value());
			auto driveKey = jsonData->get_optional<std::string>("driveKey");
			if (!driveKey.has_value() || driveKey.value().empty())
			{
				__LOG_WARN( "CLIENT_DOWNLOAD_FILES_REQUEST: " << to_string(m_id) << " invalid drive key! Data: " << data.value())
				// TODO: send error to client
				return;
			}

			for (const auto& currentFile : jsonData->get_child("files"))
			{
				const auto hash = currentFile.second.get<std::string>("hash");
				const auto pathToFile = m_storageDirectory.string() + "/" + driveKey.value() + "/drive/" + hash;

				__LOG( "CLIENT_DOWNLOAD_FILES_REQUEST full path to file" << pathToFile )

				int fileDescriptor = open(pathToFile.c_str(), O_RDONLY);
				if (fileDescriptor == -1)
				{
					__LOG_WARN( "CLIENT_DOWNLOAD_FILES_REQUEST: " << to_string(m_id) << " file not found! Path: " << pathToFile << " message: " << strerror(errno))
					// TODO: send error to client
					continue;
				}

				if (flock(fileDescriptor, LOCK_SH) == -1)
				{
					__LOG_WARN( "CLIENT_DOWNLOAD_FILES_REQUEST: " << to_string(m_id) << " Error locking file: " << pathToFile << " message: " << strerror(errno))
					close(fileDescriptor);
					// TODO: send error to client
					continue;
				}

				struct stat fileStat{};
				if (stat(pathToFile.c_str(), &fileStat) == -1)
				{
					__LOG_WARN( "CLIENT_DOWNLOAD_FILES_REQUEST: " << to_string(m_id) << " Error getting file size. File: " << pathToFile )
					close(fileDescriptor);
					// TODO: send error to client
					continue;
				}

				const auto chunkSize = libtorrent::create_torrent::automatic_piece_size(fileStat.st_size);
				const auto responseId = json->get<std::string>("id");
				sendFileDescription(responseId, driveKey.value(), hash, chunkSize, fileStat.st_size);

				uint64_t chunkIndex = 0;
				char buffer[chunkSize];
				ssize_t bytesRead;
				while ((bytesRead = read(fileDescriptor, buffer, chunkSize)) > 0)
				{
					const std::string content(buffer, bytesRead);

					Hash256 rawChunkHash;
					crypto::Sha3_256_Builder sha3;
					sha3.update(utils::RawBuffer{(const uint8_t*) content.c_str(), content.size()});
					sha3.final(rawChunkHash);

					std::ostringstream hexStream;
					hexStream << utils::HexFormat(rawChunkHash.array());
					std::string chunkHash = hexStream.str();
					const auto encodedChunk = base64_encode(content);
					chunkIndex += 1;

					sendChunk(chunkIndex, driveKey.value(), hash, chunkHash, encodedChunk);
				}

				if (bytesRead == -1)
				{
					__LOG_WARN( "CLIENT_DOWNLOAD_FILES_REQUEST: " << to_string(m_id) << "Error reading file: " << strerror(errno) << pathToFile )
				}

				close(fileDescriptor);
			}

			break;
		}

		case CLIENT_DOWNLOAD_DATA_START:
		case CLIENT_DOWNLOAD_DATA_FAILURE:
		{
			sendData(json);
			break;
		}
		case CLIENT_DOWNLOAD_DATA_ACK:
		{
			sendDataAck(json);
			break;
		}
		case SERVER_DOWNLOAD_DATA_START:
		{
			recvData(json);
			break;
		}
		case SERVER_DOWNLOAD_DATA:
		{
			// doRead();
			recvDataChunk(json);
			break;
		}
		case SERVER_DOWNLOAD_DATA_FAILURE:
		{
			recvData(json);
			break;
		}
		case CLIENT_DELETE_DATA:
		{
			deleteData(json);
			break;
		}
		case MESSAGE:
		{
			json->put("task", MESSAGE_ACK);
            // TODO: encrypt data before sending
			sendMessage(json);
			//doRead();
			break;
		}
		case CLOSE:
		{
            /* Message example: */
            /*  {
                    "payload": // encrypted by aes
                    {
                        "task":CLOSE,
                        "timestamp":""
                    },
                    "metadata":
                    {
                        "iv":"",
                        "tag":"",
                    }
                }
            */

            // TODO: re-work logic
            boost::asio::post(m_networkStrand, [pThis = shared_from_this()]()
            {
                auto closeMessage = std::make_shared<boost::property_tree::ptree>();
                closeMessage->put("payload.type", CLOSE_ACK);
                closeMessage->put("payload.timestamp", pThis->getCurrentTimestamp());

                const auto closeMessageStr = jsonToString(closeMessage);
                if (closeMessageStr.empty())
                {
                    __LOG_WARN( "Session::handleJson: session id: " << to_string(pThis->m_id) << " message: close message is empty!" )
                    pThis->m_buffer.consume(pThis->m_buffer.size());
                    pThis->doClose();
                    pThis->removeSession(pThis->m_id);
                    return;
                }

                const EncryptionResult encryptedPayload = encrypt(pThis->m_sharedKey, closeMessageStr);
                if (encryptedPayload.cipherData.empty())
                {
                    __LOG_WARN("Session::handleJson: payload encryption error")
                    pThis->m_buffer.consume(pThis->m_buffer.size());
                    pThis->doClose();
                    pThis->removeSession(pThis->m_id);
                    return;
                }

                closeMessage->put("payload", base64_encode(encryptedPayload.cipherData));
                closeMessage->put("metadata.iv", base64_encode(encryptedPayload.iv));
                closeMessage->put("metadata.tag", base64_encode(encryptedPayload.tag));

                const auto data = jsonToString(closeMessage);
                if (data.empty())
                {
                    __LOG_WARN( "Session::handleJson: session id: " << to_string(pThis->m_id) << " message: encrypted close message is empty!" )
                    pThis->m_buffer.consume(pThis->m_buffer.size());
                    pThis->doClose();
                    pThis->removeSession(pThis->m_id);
                    return;
                }

                const auto buffer = boost::asio::buffer(data);
                pThis->m_ws.async_write(buffer, [pThis](boost::beast::error_code ec, auto)
                {
                    pThis->m_buffer.consume(pThis->m_buffer.size());
                    pThis->doClose();
                    pThis->removeSession(pThis->m_id);
                    if (ec)
                    {
                        __LOG_WARN( "Session::handleErrors: " << ec.message() )
                    }
                });
            });

			break;
		}
		case CLOSE_ACK:
		{
			doClose();
			break;
		}
		default:
		{
			doClose();
		}
	}
}

void WsSession::generateSessionKeyPair(const std::string& inSessionPublicKey, std::string& outSessionPublicKey)
{
    EC_KEY* serverSessionKeys = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    if (EC_KEY_generate_key(serverSessionKeys) != 1)
    {
        __LOG_WARN( "Session::keyExchange: " << to_string(m_id) << " message: EC_KEY_generate_key error: " << getErrorMessage() )
        EC_KEY_free(serverSessionKeys);
        doClose();
        removeSession(m_id);
        return;
    }

    unsigned char* serverSessionPublicKeyRaw = nullptr;
    int serverSessionPublicKeySize = i2d_EC_PUBKEY(serverSessionKeys, &serverSessionPublicKeyRaw);
    if (serverSessionPublicKeySize <= 0)
    {
        __LOG_WARN( "Session::generateSessionKeyPair: calculation shared secret error: " << getErrorMessage() )
        EC_KEY_free(serverSessionKeys);
        doClose();
        removeSession(m_id);
        return;
    }

    outSessionPublicKey = base64_encode(std::string(reinterpret_cast<const char *>(serverSessionPublicKeyRaw), serverSessionPublicKeySize));

    const auto clientSessionPublicKey = base64_decode(inSessionPublicKey);
    const auto* clientSessionPublicKeyPtr = reinterpret_cast<const uint8_t *>(clientSessionPublicKey.c_str());

    EC_KEY* ecKeyClientSession = d2i_EC_PUBKEY(nullptr, &clientSessionPublicKeyPtr, static_cast<long>(clientSessionPublicKey.size()));
    if (!ecKeyClientSession)
    {
        __LOG_WARN( "Session::generateSessionKeyPair: Failed to load EC public key: " << getErrorMessage() )
        EC_KEY_free(serverSessionKeys);
        doClose();
        removeSession(m_id);
        return;
    }

    const auto sharedSecretSize = 32;
    m_sharedKey.resize(sharedSecretSize);
    int secret_len = ECDH_compute_key(m_sharedKey.data(), sharedSecretSize, EC_KEY_get0_public_key(ecKeyClientSession), serverSessionKeys,nullptr);
    if (secret_len <= 0)
    {
        __LOG_WARN( "Session::generateSessionKeyPair: calculation shared secret error: " << getErrorMessage() )
        EC_KEY_free(serverSessionKeys);
        EC_KEY_free(ecKeyClientSession);
        doClose();
        removeSession(m_id);
        return;
    }

    EC_KEY_free(serverSessionKeys);
    EC_KEY_free(ecKeyClientSession);
}

std::shared_ptr<boost::property_tree::ptree> WsSession::generateBasicPayload(const std::string& id, Type type)
{
    auto payload = std::make_shared<boost::property_tree::ptree>(); // !!!!! check filed names
    payload->put("id", id);
    payload->put("type", type);
    payload->put("timestamp", getCurrentTimestamp());

    return payload;
}

std::shared_ptr<boost::property_tree::ptree> WsSession::generateFinalMessage(const EncryptionResult& encryptedData)
{
    auto finalMessage = std::make_shared<boost::property_tree::ptree>();
    finalMessage->put("payload", base64_encode(encryptedData.cipherData));
    finalMessage->put("metadata.iv", base64_encode(encryptedData.iv));
    finalMessage->put("metadata.tag", base64_encode(encryptedData.tag));

    return finalMessage;
}

}
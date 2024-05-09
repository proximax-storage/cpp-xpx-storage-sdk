#include "wsserver/Session.h"
#include "wsserver/Task.h"
#include "wsserver/Base64.h"
#include "wsserver/Message.h"
#include "crypto/Signer.h"
#include "drive/log.h"
#include "utils/HexParser.h"

#include <iostream>
#include <filesystem>

#include <openssl/rand.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/ossl_typ.h>
#include <openssl/ssl.h>
#include <openssl/ec.h>
#include <openssl/pem.h>

#include <boost/archive/iterators/remove_whitespace.hpp>


// TODO: fix Man-in-the-middle attack (verify server key, authenticator app, etc)
// TODO: add logic for receipts


namespace sirius::wsserver
{

Session::Session(const boost::uuids::uuid& uuid,
                 const sirius::crypto::KeyPair& keyPair,
				 boost::asio::io_context& ioCtx,
                 boost::asio::ip::tcp::socket&& socket)
	: m_id(uuid)
	, m_strand(ioCtx)
	, m_ws(std::move(socket))
    , m_keyPair(keyPair)
{}

void Session::run()
{
	boost::asio::dispatch(m_strand, [pThis = shared_from_this()]()
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

void Session::onAccept(boost::beast::error_code ec)
{
    boost::asio::post(m_strand, [pThis = shared_from_this(), ec]()
    {
        if (ec)
        {
            // TODO: remove session if error, callback
            __LOG_WARN( "Session::onAccept: " << ec.message() )
            return;
        }

        __LOG( "Session::onAccept: Session id: " << to_string(pThis->m_id) )
        pThis->m_ws.async_read(pThis->m_buffer, [pThis](boost::beast::error_code ec, std::size_t)
        {
            if (ec == boost::beast::websocket::error::closed)
            {
                // TODO: remove session if error, callback
                __LOG( "Session::onAccept:closed: " << to_string(pThis->m_id) )
                return;
            }

            if (ec)
            {
                __LOG( "Session::onAccept:async_read: " << to_string(pThis->m_id) << " message: " << ec.message() )
                pThis->doClose();
            }
            else
            {
                const std::string message = boost::beast::buffers_to_string(pThis->m_buffer.data());
                pThis->m_buffer.consume(pThis->m_buffer.size());

                try
                {
                    std::stringstream json_input(message);
                    auto pTree = std::make_shared<boost::property_tree::ptree>();
                    boost::property_tree::read_json(json_input, *pTree);

                    auto payload = pTree->get_optional<std::string>("payload");
                    if (payload.has_value())
                    {
                        __LOG( "Session::onAccept:async_read::message: " << message )
                        pThis->keyExchange(pTree);
                    } else
                    {
                        pThis->doClose();
                    }
                } catch (const boost::property_tree::json_parser_error& e)
                {
                    __LOG_WARN( "Session::onAccept:async_read:: JSON parsing error: " << e.what() )
                    pThis->doClose();
                } catch (const std::exception& e)
                {
                    __LOG_WARN( "Session::onAccept:async_read:: An unexpected error occurred: " << e.what() )
                    pThis->doClose();
                } catch (...)
                {
                    __LOG_WARN( "Session::onAccept:async_read:: An unexpected and unknown error occurred." )
                    pThis->doClose();
                }
            }
        });
    });
}

void Session::doRead()
{
    boost::asio::post(m_strand, [pThis = shared_from_this()]()
    {
        pThis->m_ws.async_read(pThis->m_buffer, [pThis](auto ec, auto bytesTransferred)
        {
            pThis->onRead(ec, bytesTransferred);
        });
    });
}

void Session::onRead(boost::beast::error_code ec, std::size_t)
{
	if (ec == boost::beast::websocket::error::closed || ec == boost::asio::stream_errc::eof)
	{
		// TODO: remove session if error, callback
		__LOG( "Session::onRead: closed by client: " << to_string(m_id) )
		return doClose();
	}

    if (ec == boost::asio::error::operation_aborted)
    {
        // TODO: remove session if error, callback
        __LOG( "Session::onRead: operation_aborted: " << to_string(m_id)  << " message: " << ec.message() )
        return;
    }

	if (ec)
	{
		// TODO: remove session if error, callback
		__LOG_WARN( "Session::onRead::error: " << to_string(m_id) << " message: " << ec.message() )
		//doClose();
        // TODO: is close or deep analyze errors ?!
        return doRead();
	}
	else
	{
		auto pTree = std::make_shared<boost::property_tree::ptree>();
		int isAuthentic = decodeMessage(boost::beast::buffers_to_string(m_buffer.data()), pTree, m_sharedKey);
		m_buffer.consume(m_buffer.size());
        if (isAuthentic)
        {
            handleJson(pTree);
            doRead();
        }
        else
        {
            return doClose();
        }
	}
}

void Session::onWrite(boost::beast::error_code ec, std::size_t bytes_transferred)
{
	boost::ignore_unused(bytes_transferred);
	if (ec)
	{
		__LOG_WARN( "Session::onWrite::error: " << to_string(m_id) << " message: " << ec.message() )
	}
}

void Session::doClose()
{
    boost::asio::post(m_strand, [pThis = shared_from_this()]()
    {
        pThis->m_ws.async_close(boost::beast::websocket::close_code::normal, [pThis](auto ec)
        {
            pThis->onClose(ec);
        });
    });
}

void Session::onClose(boost::beast::error_code ec)
{
	if (ec)
	{
		__LOG_WARN( "Session::onClose::error: " << to_string(m_id) << " message: " << ec.message() )
	}
	else
	{
		__LOG( "Session::onClose::Session ID: " << to_string(m_id) << " is closed successfully." )
	}

	// TODO: remove session if error, callback
}

void Session::keyExchange(std::shared_ptr<boost::property_tree::ptree> json)
{
    auto encodedPayload = json->get_optional<std::string>("payload");
    if (!encodedPayload.has_value() || encodedPayload.value().empty())
    {
        // TODO: remove session
        __LOG_WARN( "Session::keyExchange: " << to_string(m_id) << " message: missed payload" )
        doClose();
        return;
    }

    auto encodedSignature = json->get_optional<std::string>("signature");
    if (!encodedSignature.has_value() || encodedSignature.value().empty())
    {
        // TODO: remove session
        __LOG_WARN( "Session::keyExchange: " << to_string(m_id) << " message: signature" )
        doClose();
        return;
    }

    auto decodedPayload = base64_decode(encodedPayload.value());
    __LOG("decodedPayload: " << decodedPayload)

    std::stringstream payloadStream(decodedPayload);
    auto payloadTree = std::make_shared<boost::property_tree::ptree>();
    boost::property_tree::read_json(payloadStream, *payloadTree);

    auto task = payloadTree->get_optional<int>("task");
    if (!task.has_value() || task.value() != Task::KEY_EX)
    {
        // TODO: remove session
        __LOG_WARN( "Session::keyExchange: " << to_string(m_id) << " message: invalid task id" )
        doClose();
        return;
    }

    auto clientPublicKey = payloadTree->get_optional<std::string>("clientPublicKey");
    if (!clientPublicKey.has_value() || clientPublicKey.value().empty())
    {
        // TODO: remove session
        __LOG_WARN( "Session::keyExchange: " << to_string(m_id) << " message: missed client public key" )
        doClose();
        return;
    }

    m_clientPublicKey = clientPublicKey.value();
    auto decodedSignature = base64_decode(encodedSignature.value());

    std::array<uint8_t, 64> rawSignature{};
    for (int i = 0; i < 64; i++)
    {
        rawSignature[i] = static_cast<uint8_t>(decodedSignature[i]);
    }

    sirius::Signature finalSignature(rawSignature);
    bool isVerified = verify( m_clientPublicKey, encodedPayload.value(), finalSignature );
    if (!isVerified)
    {
        // TODO: remove session
        __LOG_WARN( "Session::keyExchange: " << to_string(m_id) << " message: verification data error" )
        doClose();
        return;
    }

    auto clientSessionPublicKey = payloadTree->get_optional<std::string>("sessionPublicKey");
    if (!clientSessionPublicKey.has_value() || clientSessionPublicKey.value().empty())
    {
        // TODO: remove session
        __LOG_WARN( "Session::keyExchange: " << to_string(m_id) << " message: invalid client session public key" )
        doClose();
        return;
    }

    std::string serverSessionPublicKey;
    generateSessionKeyPair(clientSessionPublicKey.value(), serverSessionPublicKey);

    auto keyExchangeTree = std::make_shared<boost::property_tree::ptree>();
    keyExchangeTree->put("task", Task::KEY_EX);
    keyExchangeTree->put("sessionId",  to_string(m_id));

    std::stringstream serverPublicKey;
    serverPublicKey << sirius::utils::HexFormat(m_keyPair.publicKey().array());
    keyExchangeTree->put("serverPublicKey",  serverPublicKey.str());
    keyExchangeTree->put("serverSessionPublicKey",  serverSessionPublicKey);

    std::stringstream keyExchangeMessage;
    boost::property_tree::write_json(keyExchangeMessage, *keyExchangeTree);

    const auto encodedKeyExchangeMessage = base64_encode(keyExchangeMessage.view());
    sirius::Signature keyExchangeSignature;
    sign(m_keyPair, encodedKeyExchangeMessage, keyExchangeSignature);
    const auto encodedKeyExchangeSignature = base64_encode(keyExchangeSignature.data(), keyExchangeSignature.size());

    auto encodedMessage = std::make_shared<boost::property_tree::ptree>();
    encodedMessage->put("payload", encodedKeyExchangeMessage);
    encodedMessage->put("signature", encodedKeyExchangeSignature);

    boost::asio::post(m_strand, [pThis = shared_from_this(), encodedMessage]()
    {
        pThis->m_ws.async_write(boost::asio::buffer(jsonToString(encodedMessage)), [pThis](auto ec, auto bytesTransferred)
        {
            pThis->onWrite(ec, bytesTransferred);
        });
    });

    doRead();
}

void Session::sendMessage(std::shared_ptr<boost::property_tree::ptree> json)
{
    boost::asio::post(m_strand, [pThis = shared_from_this(), json]()
    {
        const auto buffer = boost::asio::buffer(encodeMessage(json, pThis->m_sharedKey));
        pThis->m_ws.async_write(buffer, [pThis](auto ec, auto bytesTransferred)
        {
            pThis->onWrite(ec, bytesTransferred);
        });
    });
}

void Session::recvData(std::shared_ptr<boost::property_tree::ptree> json)
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
			std::cout << "Exisiting file deleted for upload: " << m_recvDirectory[uid] << std::endl;
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

void Session::recvDataChunk(std::shared_ptr<boost::property_tree::ptree> json)
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
		json->put("task", Task::UPLOAD_FAILURE);
        sendMessage(json);
        //doRead();

        return;
	}

	if (m_recvDataCounter[uid] + 1 == m_recvNumOfDataPieces[uid])
	{
		auto pTree = std::make_shared<boost::property_tree::ptree>();
		pTree->put("task", Task::UPLOAD_ACK);
		pTree->put("uid", uid);

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

void Session::sendData(std::shared_ptr<boost::property_tree::ptree> json)
{
	auto fileName = json->get<std::string>("fileName");
	std::string filePath = "get-data/" + json->get<std::string>("drive") + json->get<std::string>("directory") + fileName;
	auto uid = json->get<std::string>("uid");

	if (!(std::filesystem::exists(filePath)))
	{
        __LOG_WARN( "Session::sendData:: file does not exist: " << filePath )

        sendMessage(json);
        //doRead();
        return;
	}

    boost::asio::post(m_strand, [pThis = shared_from_this(), json, uid, filePath, fileName]()
    {
        pThis->m_sendNumOfDataPieces[uid] = std::filesystem::file_size(filePath) / pThis->m_sendDataPieceSize;
        if (std::filesystem::file_size(filePath) % pThis->m_sendDataPieceSize != 0)
        {
            pThis->m_sendNumOfDataPieces[uid]++;
        }

        json->put("task", Task::DOWNLOAD_INFO);
        json->put("dataSize", std::filesystem::file_size(filePath));
        json->put("dataPieceSize", pThis->m_sendDataPieceSize);
        json->put("numOfDataPieces", pThis->m_sendNumOfDataPieces[uid]);
        json->put("uid", uid);

        pThis->m_sendDataCounter[uid] = 0;
        const auto buffer = boost::asio::buffer(encodeMessage(json, pThis->m_sharedKey));
        pThis->m_ws.async_write(buffer,[pThis, uid, filePath, fileName](auto ec, auto bytes_transferred)
        {
            if (!ec)
            {
                std::vector<char> fileBuffer(pThis->m_sendDataPieceSize, 0);
                std::ifstream file;
                file.open(filePath, std::ios::binary);
                if (!file.is_open())
                {
                    std::cerr << "[Download info] Failed to open file: " << filePath << std::endl;
                    return;
                }

                if (std::filesystem::exists("out_" + uid + fileName))
                {
                    // Delete the file
                    if (std::filesystem::remove("out_" + uid + fileName))
                    {
                        std::cout << "File deleted successfully." << std::endl;
                    }
                }

                while (!file.eof())
                {
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

                    auto pData = std::make_shared<boost::property_tree::ptree>();
                    pData->put("task", Task::DOWNLOAD_DATA);
                    pData->put("data", bin_data);
                    pData->put("dataPieceNum", pThis->m_sendDataCounter[uid]);
                    pData->put("uid", uid);

                    pThis->sendMessage(pData);
                    pThis->m_sendDataCounter[uid]++;
                    fileBuffer.assign(pThis->m_sendDataPieceSize, 0);
                }

                try
                {
                    // TODO: use error code instead of exceptions
                    std::filesystem::remove("out_" + uid + fileName);
                } catch (const std::filesystem::filesystem_error& e)
                {
                    __LOG_WARN( "Session::sendData::Error deleting file:" << e.what() )
                }

                //pThis->doRead();
            }
            else
            {
                // Handle errors or connection closure
                __LOG_WARN( "Session::sendData::Error in sending data:" << ec.message() )
                pThis->doClose();
            }
        });
    });
}

void Session::sendDataAck(std::shared_ptr<boost::property_tree::ptree> json)
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

void Session::deleteData(std::shared_ptr<boost::property_tree::ptree> json)
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
			json->put("task", Task::DELETE_DATA_ACK);
		}
		else
		{
			json->put("task", Task::DELETE_DATA_FAILURE);
			std::cerr << "Error deleting file: " << deleteFilePath << std::endl;
		}
	}
	else
	{
		std::cout << "File does not exist: " << deleteFilePath << std::endl;
		json->put("task", Task::DELETE_DATA_FAILURE);
	}

    sendMessage(json);
    //doRead();
};

void Session::broadcastToAll(std::shared_ptr<boost::property_tree::ptree> json)
{
//	for (const auto& session : incoming_sessions)
//	{
//		session->sendMessage(json);
//	}
}

void Session::requestToAll(std::shared_ptr<boost::property_tree::ptree> json)
{
//	for (const auto& session : incoming_sessions)
//	{
//		session->sendMessage(json);
//		session->doRead();
//	}
}

// Handles JSON receive by doRead()
void Session::handleJson(std::shared_ptr<boost::property_tree::ptree> json)
{
	__LOG( "Session::handleJson:::Session ID: " << to_string(m_id) )

	auto task = json->get_optional<int>("task");
	if (!task.has_value())
	{
		__LOG_WARN( "Session::handleJson::task error: " << to_string(m_id) << " data: " << json )
		return;
	}

	switch (task.value())
	{
		case KEY_EX:
		{
			keyExchange(json);
			break;
		}
		case DOWNLOAD_START:
		case DOWNLOAD_FAILURE:
		{
			sendData(json);
			break;
		}
		case DOWNLOAD_ACK:
		{
			sendDataAck(json);
			//doRead();
			break;
		}
		case UPLOAD_START:
		{
			recvData(json);
			break;
		}
		case UPLOAD_DATA:
		{
			// doRead();
			recvDataChunk(json);
			break;
		}
		case UPLOAD_FAILURE:
		{
			recvData(json);
			break;
		}
		case DELETE_DATA:
		{
			deleteData(json);
			break;
		}
		case MESSAGE:
		{
			json->put("task", MESSAGE_ACK);
			sendMessage(json);
			//doRead();
			break;
		}
		case CLOSE:
		{
            boost::asio::post(m_strand, [pThis = shared_from_this()]()
            {
                auto closeMessage = std::make_shared<boost::property_tree::ptree>();
                closeMessage->put("task", CLOSE_ACK);

                const auto buffer = boost::asio::buffer(encodeMessage(closeMessage, pThis->m_sharedKey));
                pThis->m_ws.async_write(buffer, [pThis](boost::beast::error_code ec, auto)
                {
                    pThis->m_buffer.consume(pThis->m_buffer.size());
                    pThis->doClose();
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

void Session::handleErrors()
{
	// TODO: Bad logic, need re-work
	__LOG_WARN( "Session::handleErrors: Error occurred" )
	ERR_print_errors_fp(stderr);
	exit(EXIT_FAILURE);
}

void Session::generateSessionKeyPair(const std::string& inSessionPublicKey, std::string& outSessionPublicKey)
{
    __LOG("Session::generateSessionKeyPair:: client session public key: " << inSessionPublicKey)

    ERR_put_error(ERR_LIB_SSL, 0, SSL_R_SSL_HANDSHAKE_FAILURE, __FILE__, __LINE__);
    EC_KEY* serverSessionKeys = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    if (EC_KEY_generate_key(serverSessionKeys) != 1)
    {
        // TODO: remove session
        BIO* bioErrors = BIO_new(BIO_s_mem());
        ERR_print_errors(bioErrors);
        char* errors;
        long len = BIO_get_mem_data(bioErrors, &errors);
        std::string finalError(errors, len + 1 );

        __LOG_WARN( "Session::keyExchange: " << to_string(m_id) << " message: EC_KEY_generate_key error: " << finalError )

        BIO_free(bioErrors);
        EC_KEY_free(serverSessionKeys);
        doClose();
        return;
    }

    unsigned char* serverSessionPublicKeyRaw = nullptr;
    int serverSessionPublicKeySize = i2d_EC_PUBKEY(serverSessionKeys, &serverSessionPublicKeyRaw);
    if (serverSessionPublicKeySize <= 0)
    {
        // TODO: remove session
        __LOG_WARN( "Session::generateSessionKeyPair: calculation shared secret error" )
        //fprintf(stderr, "Failed to encode public key.\n");
        EC_KEY_free(serverSessionKeys);
        doClose();
        return;
    }

    outSessionPublicKey = base64_encode(std::string(reinterpret_cast<const char *>(serverSessionPublicKeyRaw), serverSessionPublicKeySize));

    __LOG("Session::generateSessionKeyPair:: server session public key: " << outSessionPublicKey)

    const auto clientSessionPublicKey = base64_decode(inSessionPublicKey);
    const auto* clientSessionPublicKeyPtr = reinterpret_cast<const uint8_t *>(clientSessionPublicKey.c_str());

    EC_KEY* ecKeyClientSession = d2i_EC_PUBKEY(nullptr, &clientSessionPublicKeyPtr, static_cast<long>(clientSessionPublicKey.size()));
    if (!ecKeyClientSession)
    {
        // TODO: remove session
        __LOG_WARN( "Session::generateSessionKeyPair: Failed to load EC public key" )
        ERR_print_errors_fp(stderr);
        EC_KEY_free(serverSessionKeys);
        doClose();
        return;
    }

    const auto sharedSecretSize = 32;
    m_sharedKey.resize(sharedSecretSize);
    int secret_len = ECDH_compute_key(m_sharedKey.data(), sharedSecretSize, EC_KEY_get0_public_key(ecKeyClientSession), serverSessionKeys,nullptr);
    if (secret_len <= 0)
    {
        // TODO: remove session, fix logs
        __LOG_WARN( "Session::generateSessionKeyPair: calculation shared secret error" )
        ERR_print_errors_fp(stderr);
        EC_KEY_free(serverSessionKeys);
        EC_KEY_free(ecKeyClientSession);
        doClose();
        return;
    }

    __LOG("Session shared secret: " << base64_encode( m_sharedKey))

    EC_KEY_free(serverSessionKeys);
    EC_KEY_free(ecKeyClientSession);
}

}
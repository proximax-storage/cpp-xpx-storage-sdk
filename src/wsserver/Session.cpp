#include "wsserver/Session.h"
#include "wsserver/Task.h"
#include "wsserver/Base64.h"
#include "wsserver/Message.h"
#include "drive/log.h"
#include <iostream>
#include <filesystem>

#include <openssl/rand.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/dh.h>
#include <openssl/ossl_typ.h>


namespace sirius::wsserver
{

Session::Session(const boost::uuids::uuid& uuid,
				 boost::asio::io_context& ioCtx,
				 std::unique_ptr<boost::asio::ip::tcp::socket> socket,
				 const boost::asio::ip::tcp::endpoint::protocol_type& protocolType)
	: m_id(uuid)
	, m_strand(ioCtx)
	, m_socket(std::move(socket))
	, m_ws(m_strand, protocolType, m_socket->native_handle())
{}

void Session::run()
{
	boost::asio::dispatch(m_strand, [pThis = shared_from_this()]() { pThis->onRun(); });
}

void Session::onRun()
{
	m_ws.set_option(boost::beast::websocket::stream_base::timeout::suggested(boost::beast::role_type::server));
	m_ws.set_option(boost::beast::websocket::stream_base::decorator([](boost::beast::websocket::response_type& res)
	{
		res.set(boost::beast::http::field::server, std::string(BOOST_BEAST_VERSION_STRING) + " websocket-server-async");
	}));

	m_ws.async_accept(boost::beast::bind_front_handler(&Session::onAccept, shared_from_this()));
}

void Session::onAccept(boost::beast::error_code ec)
{
	if (ec)
	{
		// TODO: remove session if error, callback
		__LOG_WARN( "Session::onAccept: " << ec.message() )
		return;
	}

	__LOG( "Session::onAccept: " << to_string(m_id) )
	m_ws.async_read(m_buffer, [this](boost::beast::error_code ec, std::size_t bytes_transferred)
	{
		boost::ignore_unused(bytes_transferred);
		if (ec == boost::beast::websocket::error::closed)
		{
			// TODO: remove session if error, callback
			__LOG( "Session::onAccept:closed: " << to_string(m_id) )
			return;
		}

		if (ec)
		{
			__LOG( "Session::onAccept:async_read: " << to_string(m_id) << " message: " << ec.message() )
			doClose();
		}
		else
		{
			const std::string message = boost::beast::buffers_to_string(m_buffer.data());
			m_buffer.consume(m_buffer.size());

			try
			{
				std::istringstream json_input(message);
				auto pTree = std::make_shared<boost::property_tree::ptree>();
				boost::property_tree::read_json(json_input, *pTree);

				auto task = pTree->get_optional<int>("task");
				if (task.has_value() && task.value() == Task::KEY_EX)
				{
					__LOG( "Session::onAccept:async_read::message: " << message )
					keyExchange(pTree);
				} else
				{
					doClose();
				}
			} catch (const boost::property_tree::json_parser_error& e)
			{
				__LOG_WARN( "Session::onAccept:async_read:: JSON parsing error: " << e.what() )
				doClose();
			} catch (const std::exception& e)
			{
				__LOG_WARN( "Session::onAccept:async_read:: An unexpected error occurred: " << e.what() )
				doClose();
			} catch (...)
			{
				__LOG_WARN( "Session::onAccept:async_read:: An unexpected and unknown error occurred." )
				doClose();
			}
		}
	});
}

void Session::doRead()
{
	m_ws.async_read(m_buffer, boost::beast::bind_front_handler(&Session::onRead, shared_from_this()));
}

void Session::onRead(boost::beast::error_code ec, std::size_t bytes_transferred)
{
	boost::ignore_unused(bytes_transferred);
	if (ec == boost::beast::websocket::error::closed)
	{
		// TODO: remove session if error, callback
		__LOG( "Session::onRead:closed: " << to_string(m_id) )
		return;
	}

	if (ec)
	{
		// TODO: remove session if error, callback
		__LOG_WARN( "Session::onRead::error: " << to_string(m_id) << " message: " << ec.message() )
		doClose();
	}
	else
	{
		auto pTree = std::make_shared<boost::property_tree::ptree>();
		int isAuthentic = decodeMessage(boost::beast::buffers_to_string(m_buffer.data()), pTree, m_sharedKey);
		m_buffer.consume(m_buffer.size());
		isAuthentic ? handleJson(pTree) : doClose();
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
	m_ws.async_close(boost::beast::websocket::close_code::normal, boost::beast::bind_front_handler(&Session::onClose, shared_from_this()));
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
	OpenSSL_add_all_algorithms();
	ERR_load_crypto_strings();

	auto pDh = std::shared_ptr<DH>(DH_new(), [](DH* dh){ DH_free(dh); });
	if (!pDh)
	{
		handleErrors();
	}

	auto bigNumDeleter = [](BIGNUM* pValue){ BN_free(pValue); };

	auto pRawPBN = BN_new();
	BN_hex2bn(&pRawPBN, AES_P.c_str());
	auto pPBN = std::shared_ptr<BIGNUM>(pRawPBN, bigNumDeleter);

	auto pRawGBN = BN_new();
	BN_hex2bn(&pRawGBN, AES_G.c_str());
	auto pGBN = std::shared_ptr<BIGNUM>(pRawGBN, bigNumDeleter);

	DH_set0_pqg(pDh.get(), pPBN.get(), nullptr, pGBN.get());
	// Generate public and private keys for Server
	if (DH_generate_key(pDh.get()) != 1)
	{
		handleErrors();
	}

	// Serialize public key of Server to send to Bob
	const auto* pRawServerPublicKey = BN_new();
	DH_get0_key(pDh.get(), &pRawServerPublicKey, nullptr);
	auto pServerPublicKey = std::shared_ptr<BIGNUM>(const_cast<BIGNUM*>(pRawServerPublicKey), bigNumDeleter);
	auto serializedSeverPublicKey = BN_bn2hex(pServerPublicKey.get());

	// Send serializedSeverPublicKey to connected Client
	boost::property_tree::ptree key;
	key.put("task", Task::KEY_EX);
	key.put("publicKey", serializedSeverPublicKey);

	std::ostringstream keyStream;
	write_json(keyStream, key, false);

	// TODO: Use async call
	m_ws.write(boost::asio::buffer(keyStream.str()));

	auto pRawDeserializedClientPublicKey = BN_new();
	auto clientPublicKeyStr = json->get<std::string>("publicKey");
	BN_hex2bn(&pRawDeserializedClientPublicKey, clientPublicKeyStr.c_str());
	auto pDeserializedClientPublicKey = std::shared_ptr<BIGNUM>(pRawDeserializedClientPublicKey, bigNumDeleter);

	// Allocate memory for sharedSecret
	auto opensslDeleter = [](void* pValue) { OPENSSL_free(pValue); };
	std::shared_ptr<unsigned char> pSharedSecret((unsigned char*)OPENSSL_malloc(DH_size(pDh.get())), opensslDeleter);

	if (DH_compute_key(pSharedSecret.get(), pDeserializedClientPublicKey.get(), pDh.get()) == -1)
	{
		handleErrors();
	}

	std::string derivedKey;
	for (int i = 0; i < DH_size(pDh.get()); ++i)
	{
		std::stringstream ss;
		ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(pSharedSecret.get()[i]);
		derivedKey += ss.str();
	}

	m_sharedKey = derivedKey.substr(0, 32);
	ERR_free_strings();
	doRead();
}

void Session::sendMessage(std::shared_ptr<boost::property_tree::ptree> json)
{
	const auto buffer = boost::asio::buffer(encodeMessage(json, m_sharedKey));
	m_ws.async_write(buffer, boost::beast::bind_front_handler(&Session::onWrite, shared_from_this()));
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

	doRead();
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
		std::cerr << "Error appending data to file: " << json->get<std::string>("fileName") << std::endl;
		json->put("task", Task::UPLOAD_FAILURE);
		m_ws.async_write(boost::asio::buffer(encodeMessage(json, m_sharedKey)), [this](boost::beast::error_code ec, std::size_t bytes_transferred) { doRead(); });
		return;
		file.close(); // Close the file before exiting
		return;
	}

	file.close();

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

	doRead();
}

void Session::sendData(std::shared_ptr<boost::property_tree::ptree> json)
{
	auto fileName = json->get<std::string>("fileName");
	std::string filePath = "get-data/" + json->get<std::string>("drive") + json->get<std::string>("directory") + fileName;
	auto uid = json->get<std::string>("uid");

	if (!(std::filesystem::exists(filePath))) 
	{
		std::cout << filePath << " does not exist" << std::endl;
		json->put("task", Task::DOWNLOAD_FAILURE);
		m_ws.async_write(
				boost::asio::buffer(encodeMessage(json, m_sharedKey)),
				[this](boost::beast::error_code ec, std::size_t bytes_transferred) { doRead(); });
		return;
	}

	m_sendNumOfDataPieces[uid] = std::filesystem::file_size(filePath) / m_sendDataPieceSize;
	if (std::filesystem::file_size(filePath) % m_sendDataPieceSize != 0) 
	{
		m_sendNumOfDataPieces[uid]++;
	}

	json->put("task", Task::DOWNLOAD_INFO);
	json->put("dataSize", std::filesystem::file_size(filePath));
	json->put("dataPieceSize", m_sendDataPieceSize);
	json->put("numOfDataPieces", m_sendNumOfDataPieces[uid]);
	json->put("uid", uid);

	m_sendDataCounter[uid] = 0;

	m_ws.async_write(
			boost::asio::buffer(encodeMessage(json, m_sharedKey)),
			[this, uid, filePath, fileName](boost::beast::error_code ec, std::size_t bytes_transferred)
			{
				std::cout << "Download info sent: " << filePath << std::endl;
				if (!ec)
				{
					std::vector<char> fileBuffer(m_sendDataPieceSize, 0);
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
						pData->put("dataPieceNum", m_sendDataCounter[uid]);
						pData->put("uid", uid);

						// TODO: Use async call
						m_ws.write(boost::asio::buffer(encodeMessage(pData, m_sharedKey)));

						m_sendDataCounter[uid]++;
						fileBuffer.assign(m_sendDataPieceSize, 0);
					}

					try
					{
						std::filesystem::remove("out_" + uid + fileName);
					} catch (const std::filesystem::filesystem_error& e)
					{
						std::cerr << "Error deleting file: " << e.what() << std::endl;
					}

					doRead();
				}
				else
				{
					// Handle errors or connection closure
					std::cerr << "Error in sending data: " << ec.message() << std::endl;
					doClose();
				}
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

	m_ws.async_write(
			boost::asio::buffer(encodeMessage(json, m_sharedKey)),
			[this](boost::beast::error_code ec, std::size_t bytes_transferred) { doRead(); });
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
			doRead();
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
			doRead();
			break;
		}
		case CLOSE:
		{
			auto closeMessage = std::make_shared<boost::property_tree::ptree>();
			closeMessage->put("task", CLOSE_ACK);

			const auto buffer = boost::asio::buffer(encodeMessage(closeMessage, m_sharedKey));
			m_ws.async_write(buffer, [pThis = shared_from_this()](boost::beast::error_code ec, auto)
			{
				pThis->m_buffer.consume(pThis->m_buffer.size());
				pThis->doClose();
				if (ec)
				{
					__LOG_WARN( "Session::handleErrors: " << ec.message() )
				}
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

}
#ifndef SESSION_H
#define SESSION_H

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
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

#include "crypto/KeyPair.h"

namespace sirius::wsserver
{

#define FILE_CHUNK_SIZE 1024 * 1024;

class Session : public std::enable_shared_from_this<Session>
{
	public:
		explicit Session(const boost::uuids::uuid& uuid,
                         const sirius::crypto::KeyPair& keyPair,
						 boost::asio::io_context& ioCtx,
                         boost::asio::ip::tcp::socket&& socket);
		~Session() = default;

	public:
		void run();
		void onAccept(boost::beast::error_code ec);
		void doRead();
		void onRead(boost::beast::error_code ec, std::size_t bytes_transferred);
		void onWrite(boost::beast::error_code ec, std::size_t bytes_transferred);
		void doClose();
		void onClose(boost::beast::error_code ec);
		void keyExchange(std::shared_ptr<boost::property_tree::ptree> json);
		void sendMessage(std::shared_ptr<boost::property_tree::ptree> json);
		void recvData(std::shared_ptr<boost::property_tree::ptree> json);
		void recvDataChunk(std::shared_ptr<boost::property_tree::ptree> json);
		void sendData(std::shared_ptr<boost::property_tree::ptree> json);
		void sendDataAck(std::shared_ptr<boost::property_tree::ptree> json);
		void deleteData(std::shared_ptr<boost::property_tree::ptree> json);

		void broadcastToAll(std::shared_ptr<boost::property_tree::ptree> json);
		void requestToAll(std::shared_ptr<boost::property_tree::ptree> json);

	private:
		void handleJson(std::shared_ptr<boost::property_tree::ptree> json);
		void handleErrors();
        void generateSessionKeyPair(const std::string& inSessionPublicKey, std::string& outSessionPublicKey);

	private:
		std::string m_sharedKey;
        std::string m_clientPublicKey;

		std::unordered_map<std::string, std::string> m_recvDirectory;
		std::unordered_map<std::string, int> m_recvNumOfDataPieces;
		std::unordered_map<std::string, int> m_recvDataCounter;

		std::unordered_map<std::string, int> m_sendNumOfDataPieces;
		const std::size_t m_sendDataPieceSize = FILE_CHUNK_SIZE;
		std::unordered_map<std::string, int> m_sendDataCounter;

        boost::uuids::uuid m_id;
        boost::asio::io_context::strand m_strand;
		boost::beast::websocket::stream<boost::beast::tcp_stream> m_ws;
		boost::beast::flat_buffer m_buffer;

        const sirius::crypto::KeyPair& m_keyPair;
};
}

#endif // SESSION_H

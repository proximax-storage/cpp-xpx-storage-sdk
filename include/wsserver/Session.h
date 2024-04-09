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

namespace sirius::wsserver
{

#define FILE_CHUNK_SIZE 1024 * 1024;

class Session : public std::enable_shared_from_this<Session>
{
	public:
		explicit Session(const boost::uuids::uuid& uuid,
						 boost::asio::io_context& ioCtx,
						 std::unique_ptr<boost::asio::ip::tcp::socket> socket,
						 const boost::asio::ip::tcp::endpoint::protocol_type& protocolType);
		~Session() = default;

	public:
		void run();
		void onRun();
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

	private:
		std::string m_sharedKey; // Shared secret key for encrypt/decrypt/hashing

		std::unordered_map<std::string, std::string> m_recvDirectory;
		std::unordered_map<std::string, int> m_recvNumOfDataPieces;
		std::unordered_map<std::string, int> m_recvDataCounter;

		std::unordered_map<std::string, int> m_sendNumOfDataPieces;
		const std::size_t m_sendDataPieceSize = FILE_CHUNK_SIZE;
		std::unordered_map<std::string, int> m_sendDataCounter;

	private:
		std::unique_ptr<boost::asio::ip::tcp::socket> m_socket;
		boost::beast::websocket::stream<boost::beast::tcp_stream> m_ws;
		boost::beast::flat_buffer m_buffer;
		boost::asio::io_context::strand m_strand;
		boost::uuids::uuid m_id;

		const std::string AES_P = "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD129024E088A67CC74020BBEA63B139B22514A08798E3404DDEF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7EDEE386BFB5A899FA5AE9F24117C4B1FE649286651ECE45B3DC2007CB8A163BF0598DA48361C55D39A69163FA8FD24CF5F83655D23DCA3AD961C62F356208552BB9ED529077096966D670C354E4ABC9804F1746C08CA18217C32905E462E36CE3BE39E772C180E86039B2783A2EC07A28FB5C55DF06F4C52C9DE2BCBF6955817183995497CEA956AE515D2261898FA051015728E5A8AACAA68FFFFFFFFFFFFFFFF";
		const std::string AES_G = "2";
};
}

#endif // SESSION_H

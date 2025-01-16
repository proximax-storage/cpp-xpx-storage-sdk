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
#include <boost/uuid/random_generator.hpp>

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
#include "wsserver/Task.h"
#include "EncryptDecrypt.h"

namespace sirius::wsserver
{

#define FILE_CHUNK_SIZE 1024 * 1024;

class WsSession : public std::enable_shared_from_this<WsSession>
{
	public:
		explicit WsSession(const boost::uuids::uuid& uuid,
                         const sirius::crypto::KeyPair& keyPair,
						 boost::asio::io_context& ioCtx,
                         boost::asio::ip::tcp::socket&& socket,
                         std::filesystem::path& storageDirectory,
                         std::function<void(boost::property_tree::ptree data, std::function<void(boost::property_tree::ptree fsTreeJson)> callback)> fsTreeHandler,
                         std::function<void(const boost::uuids::uuid& id)> remover);
		~WsSession() = default;

	public:
		void run();
		void onAccept(boost::beast::error_code ec);
		void doRead();
		void onRead(boost::beast::error_code ec, std::size_t bytes_transferred);
		void onWrite(boost::beast::error_code ec, std::size_t bytes_transferred);
		void doClose();
        void doClose(ServerErrorCode code, const std::string& message);
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
		void sendChunk(uint64_t chunkIndex,
					   const std::string& driveKey,
					   const std::string& fileHash,
					   const std::string& chunkHash,
					   const std::string& chunk);

		void sendFileDescription(const std::string& responseId,
								 const std::string& driveKey,
								 const std::string& fileHash,
								 int chunkSize,
								 uint64_t fileSize);

        void handleUploadDataStartRequest(std::shared_ptr<boost::property_tree::ptree> json);
        void handleUploadDataRequest(std::shared_ptr<boost::property_tree::ptree> json);
        bool isValidUUIDv4(const std::string& uuid);
        bool validateUploadDataStartRequest(std::shared_ptr<boost::property_tree::ptree> json);
        bool validateUploadDataRequest(std::shared_ptr<boost::property_tree::ptree> json);
        long getCurrentTimestamp();
        std::string generateMessageId();
		void handlePayload(std::shared_ptr<boost::property_tree::ptree> json);
        void generateSessionKeyPair(const std::string& inSessionPublicKey, std::string& outSessionPublicKey);
        std::shared_ptr<boost::property_tree::ptree> generateBasicPayload(const std::string& id, Type type);
        std::shared_ptr<boost::property_tree::ptree> generateFinalMessage(const EncryptionResult& encryptedData);

	private:
		std::string m_sharedKey;
        std::string m_clientPublicKey;
        std::filesystem::path& m_storageDirectory;

        std::function<void(boost::property_tree::ptree data, std::function<void(boost::property_tree::ptree fsTreeJson)> callback)> fsTreeHandler;
        std::function<void(const boost::uuids::uuid& id)> removeSession;
		std::unordered_map<std::string, std::string> m_recvDirectory;
		std::unordered_map<std::string, int> m_recvNumOfDataPieces;
		std::unordered_map<std::string, int> m_recvDataCounter;

		std::unordered_map<std::string, int> m_sendNumOfDataPieces;
		const std::size_t m_sendDataPieceSize = FILE_CHUNK_SIZE;
		std::unordered_map<std::string, int> m_sendDataCounter;

        boost::uuids::uuid m_id;
        boost::uuids::random_generator m_uuidGenerator;
        boost::asio::io_context::strand m_networkStrand;
        boost::asio::io_context::strand m_downloadsStrand;
		boost::beast::websocket::stream<boost::beast::tcp_stream> m_ws;
		boost::beast::flat_buffer m_buffer;

        const sirius::crypto::KeyPair& m_keyPair;

    private:
        struct FileDescriptor
        {
            FileDescriptor() = default;
            ~FileDescriptor() = default;

            FileDescriptor(const std::string& name,
                           const std::filesystem::path& path,
                           const std::filesystem::path& relativePath,
                           const std::string& driveKey,
                           const std::string& hash,
                           std::uint64_t size,
                           std::uint64_t chunksAmount,
                           unsigned int chunkSize)
                    : m_name(name)
                    , m_path(path)
                    , m_relativePath(relativePath)
                    , m_driveKey(driveKey)
                    , m_hash(hash)
                    , m_size(size)
                    , m_chunksAmount(chunksAmount)
                    , m_chunkSize(chunkSize)
            {}

            FileDescriptor(const FileDescriptor& other)
                    : m_name(other.m_name)
                    , m_path(other.m_path)
                    , m_relativePath(other.m_relativePath)
                    , m_driveKey(other.m_driveKey)
                    , m_hash(other.m_hash)
                    , m_size(other.m_size)
                    , m_chunksAmount(other.m_chunksAmount)
                    , m_chunkSize(other.m_chunkSize)
            {}

            FileDescriptor(FileDescriptor&& other) noexcept
                    : m_name(std::move(other.m_name))
                    , m_path(std::move(other.m_path))
                    , m_relativePath(std::move(other.m_relativePath))
                    , m_driveKey(std::move(other.m_driveKey))
                    , m_hash(std::move(other.m_hash))
                    , m_size(other.m_size)
                    , m_chunksAmount(other.m_chunksAmount)
                    , m_chunkSize(other.m_chunkSize)
            {}

            FileDescriptor& operator=(const FileDescriptor& other)
            {
                if (this != &other)
                {
                    m_name = other.m_name;
                    m_path = other.m_path;
                    m_relativePath = other.m_relativePath;
                    m_driveKey = other.m_driveKey;
                    m_hash = other.m_hash;
                    m_size = other.m_size;
                    m_chunksAmount = other.m_chunksAmount;
                    m_chunkSize = other.m_chunkSize;
                }

                return *this;
            }

            FileDescriptor& operator=(FileDescriptor&& other) noexcept
            {
                if (this != &other)
                {
                    m_name = std::move(other.m_name);
                    m_path = std::move(other.m_path);
                    m_relativePath = std::move(other.m_relativePath);
                    m_driveKey = std::move(other.m_driveKey);
                    m_hash = std::move(other.m_hash);
                    m_size = other.m_size;
                    m_chunksAmount = other.m_chunksAmount;
                    m_chunkSize = other.m_chunkSize;
                }

                return *this;
            }

            std::string m_name;
            std::filesystem::path m_path;
            std::filesystem::path m_relativePath;
            std::string m_driveKey;
            std::string m_hash;
            std::unique_ptr<std::ofstream> m_stream;
            std::uint64_t m_size; // bytes
            std::uint64_t m_chunksAmount;
            unsigned int m_chunkSize; // bytes
        };

        std::unordered_map<std::string, FileDescriptor> m_downloads;
};
}

#endif // SESSION_H

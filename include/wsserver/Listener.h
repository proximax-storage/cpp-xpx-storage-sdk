#ifndef LISTENER_H
#define LISTENER_H

#include "plugins.h"
#include "drive/log.h"
#include "types.h"
#include "wsserver/WsSession.h"
#include "crypto/KeyPair.h"

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/strand.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/beast/core/buffers_to_string.hpp>

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

class PLUGIN_API Listener : public std::enable_shared_from_this<Listener>
{
	public:
		Listener(boost::asio::io_context& ioCtx, const sirius::crypto::KeyPair& keyPair, const std::string& storageDirectory)
			: m_keyPair(keyPair)
            , m_ioCtx(ioCtx)
			, m_acceptor(ioCtx)
			, m_strand(ioCtx)
            , m_storageDirectory(std::filesystem::current_path() / std::filesystem::path(storageDirectory))
		{}

	public:
		void init(const boost::asio::ip::tcp::endpoint& endpoint);
		void run();
        void setFsTreeHandler(std::function<void(boost::property_tree::ptree data,
							  std::function<void(boost::property_tree::ptree fsTreeJson)> callback)> handler);

        void setAddModificationHandler(std::function<void(Key driveKey,
														  std::array<uint8_t,32> modificationId,
														  std::function<void()> onModificationStarted)> handler);

        void setModificationFilesHandler(std::function<void(Key driveKey,
															std::array<uint8_t,32> modificationId,
															std::filesystem::path actionListPath,
															std::filesystem::path folderWithFiles,
															std::function<void(bool)> onModificationFilesCouldBeRemoved)> handler);

	private:
		void doAccept();
		void onAccept(boost::beast::error_code ec, boost::asio::ip::tcp::socket&& socket);
        void removeSession(const boost::uuids::uuid& id);

	private:
        const sirius::crypto::KeyPair& m_keyPair;
		boost::asio::ip::tcp::endpoint m_endpoint;
		boost::asio::io_context& m_ioCtx;
		boost::asio::ip::tcp::acceptor m_acceptor;
		boost::asio::io_context::strand m_strand;
		boost::uuids::random_generator m_uuidGenerator;
        std::filesystem::path m_storageDirectory;
		std::map<boost::uuids::uuid, std::shared_ptr<WsSession>> m_sessions;

        std::function<void(boost::property_tree::ptree, std::function<void(boost::property_tree::ptree)>)> m_fsTreeHandler;
		std::function<void(Key, std::array<uint8_t,32>, std::function<void()>)> m_addModificationHandler;
		std::function<void(Key, std::array<uint8_t,32>, std::filesystem::path, std::filesystem::path, std::function<void(bool)>)> m_modificationFilesHandler;
};
};

#endif // LISTENER_H
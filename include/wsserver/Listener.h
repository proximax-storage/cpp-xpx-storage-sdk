#ifndef LISTENER_H
#define LISTENER_H

#include "plugins.h"
#include "drive/log.h"
#include "wsserver/Session.h"
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
		Listener(boost::asio::io_context& ioCtx, const sirius::crypto::KeyPair& keyPair)
			: m_keyPair(keyPair)
            , m_ioCtx(ioCtx)
			, m_acceptor(ioCtx)
			, m_strand(ioCtx)
		{}

	public:
		void init(const boost::asio::ip::tcp::endpoint& endpoint);
		void run();

	private:
		void doAccept();
		void onAccept(boost::beast::error_code ec, boost::asio::ip::tcp::socket&& socket);

	private:
        const sirius::crypto::KeyPair& m_keyPair;
		boost::asio::ip::tcp::endpoint m_endpoint;
		boost::asio::io_context& m_ioCtx;
		boost::asio::ip::tcp::acceptor m_acceptor;
		boost::asio::io_context::strand m_strand;
		boost::uuids::random_generator m_uuidGenerator;
		std::map<boost::uuids::uuid, std::shared_ptr<Session>> m_sessions;
};
};

#endif // LISTENER_H
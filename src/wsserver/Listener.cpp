#include "wsserver/Listener.h"

namespace sirius::wsserver
{

void Listener::init(const boost::asio::ip::tcp::endpoint& endpoint)
{
	m_endpoint = endpoint;
	boost::beast::error_code ec;
	m_acceptor.open(endpoint.protocol(), ec);
	if(ec)
	{
		_LOG_ERR( "Listener::acceptor.open: " << ec.message() )
		return;
	}

	m_acceptor.set_option(boost::asio::socket_base::reuse_address(true), ec);
	if(ec)
	{
		_LOG_ERR( "Listener::acceptor.set_option: " << ec.message() )
		return;
	}

	m_acceptor.bind(endpoint, ec);
	if(ec)
	{
		_LOG_ERR( "Listener::acceptor.acceptor.bind: " << ec.message() )
		return;
	}

	m_acceptor.listen(boost::asio::socket_base::max_listen_connections, ec);
	if(ec)
	{
		_LOG_ERR( "Listener::acceptor.listen: " << ec.message() )
		return;
	}
}

void Listener::run()
{
    doAccept();
}

void Listener::setFsTreeHandler(std::function<void(boost::property_tree::ptree data, std::function<void(boost::property_tree::ptree fsTreeJson)> callback)> handler)
{
    m_fsTreeHandler = handler;
}

void Listener::setAddModificationHandler(std::function<void(Key driveKey,
										 std::array<uint8_t,32> modificationId,
										 std::function<void()> onModificationStarted)> handler)
{
	m_addModificationHandler = handler;
}

void Listener::setModificationFilesHandler(std::function<void(Key driveKey,
										   std::array<uint8_t,32> modificationId,
										   std::filesystem::path actionListPath,
										   std::filesystem::path folderWithFiles,
										   std::function<void(bool)> onModificationFilesCouldBeRemoved)> handler)
{
	m_modificationFilesHandler = handler;
}

void Listener::doAccept()
{
	m_acceptor.async_accept(m_ioCtx,[pThis = shared_from_this()](auto ec, auto socket)
	{
		pThis->onAccept(ec, std::move(socket));
	});
}

void Listener::onAccept(boost::beast::error_code ec, boost::asio::ip::tcp::socket&& socket)
{
    if(ec)
    {
		__LOG_WARN( "Listener::onAccept: " << ec.message() )
    }
    else
    {
		boost::asio::post(m_strand, [pThis = shared_from_this(), pSocket = std::move(socket)]() mutable
		{
			auto sessionId = pThis->m_uuidGenerator();
            auto callback = [pThis](const boost::uuids::uuid& id){ pThis->removeSession(id); };
			auto newSession = std::make_shared<WsSession>(sessionId,
                                                        pThis->m_keyPair,
                                                        pThis->m_ioCtx,
                                                        std::move(pSocket),
                                                        pThis->m_storageDirectory,
                                                        pThis->m_fsTreeHandler,
                                                        callback);

			pThis->m_sessions.insert({sessionId, std::move(newSession) });
			pThis->m_sessions[sessionId]->run();
		});
    }

    doAccept();
}

void Listener::removeSession(const boost::uuids::uuid& id)
{
    boost::asio::post(m_strand, [pThis = shared_from_this(), id]()
    {
        if (pThis->m_sessions.erase(id) == 0)
        {
            __LOG_WARN( "Listener::removeSession: session not found: " << to_string(id) )
        }
    });
}

}
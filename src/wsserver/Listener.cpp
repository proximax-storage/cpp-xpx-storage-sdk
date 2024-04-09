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

void Listener::doAccept()
{
	m_acceptor.async_accept(m_ioCtx,[pThis = shared_from_this()](auto ec, auto socket)
	{
		auto newSocket = std::make_unique<boost::asio::ip::tcp::socket>(std::move(socket));
		pThis->onAccept(ec, std::move(newSocket));
	});
}

void Listener::onAccept(boost::beast::error_code ec, std::unique_ptr<boost::asio::ip::tcp::socket> socket)
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
			auto newSession = std::make_unique<Session>(sessionId, pThis->m_ioCtx, std::move(pSocket), pThis->m_endpoint.protocol());
			pThis->m_sessions.insert({sessionId, std::move(newSession) });
			pThis->m_sessions[sessionId]->run();
		});
    }

    doAccept();
}

}
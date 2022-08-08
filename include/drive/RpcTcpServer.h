//
//  RpcTcpServer.h
//
//  Created by Aleksander Tsarenko on 12.07.22.
//

#pragma once

#include <iostream>
#include <optional>
#include <functional>
#include <sstream>
#include <boost/asio.hpp>

#include <cereal/types/string.hpp>
#include <cereal/types/array.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/map.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/archives/portable_binary.hpp>

#include "RpcReplicatorCommands.h"


namespace asio = boost::asio;
using     tcp  = boost::asio::ip::tcp;

class RpcTcpServer : public std::enable_shared_from_this<RpcTcpServer>
{
protected:

    class Session;

    std::thread                             m_thread;
    asio::io_context                        m_context;
    std::optional<asio::ip::tcp::acceptor>  m_acceptor;
    std::optional<asio::ip::tcp::socket>    m_socket;
    std::shared_ptr<Session>                m_upSession; // up command channel - from remote process to RPC server
    std::shared_ptr<Session>                m_dnSession; // dn command channel - from RPC server to remote process
    bool									m_isConnectionLost = false;

protected:

    RpcTcpServer()
    {
        __LOG( "RpcTcpServer(); address: " )
    }
    
    virtual ~RpcTcpServer()
    {
        closeSockets();
        m_context.stop();
        if ( m_thread.joinable() )
        {
            m_thread.join();
        }
    }

    virtual void initiateRemoteService() = 0;

    virtual void handleCommand( RPC_CMD command, cereal::PortableBinaryInputArchive* parameters ) = 0;

    virtual void handleError( std::error_code ) = 0;

    virtual void handleConnectionLost() = 0;
    
    virtual void dbgEmulateSignal( int index ) = 0;
    
    void rpcCall( RPC_CMD func, const std::string& parameters );
    
    std::array<uint8_t,32> rpcDbgGetRootHash( const std::string& parameters );

public:
    void startTcpServer( std::string address, std::uint16_t port )
    {
        m_acceptor.emplace( m_context, asio::ip::tcp::endpoint( boost::asio::ip::make_address(address.c_str()), port ) );

        async_accept();
        
        m_thread = std::thread( [this]
        {
            m_context.run();
        });
    }
    
protected:
    // Session -
    class Session : public std::enable_shared_from_this<Session>
    {
        std::weak_ptr<RpcTcpServer> m_server;
        
        asio::ip::tcp::socket   socket;
        asio::streambuf         streambuf;
        std::vector<uint8_t>    in_buff;

        uint16_t                packetLen;
        RPC_CMD                 command;

    public:

        Session( asio::ip::tcp::socket&& socket, std::weak_ptr<RpcTcpServer> server )
            : m_server(server), socket( std::move(socket) )
        {
        }

        void start()
        {
            __LOG( "start()" );
            asio::async_read( socket, asio::buffer( &command, sizeof(command) ),
                                      asio::transfer_exactly( sizeof(command) ),
                             [self = shared_from_this()] ( boost::system::error_code error, std::size_t bytes_transferred )
            {
                if ( !error )
                {
                    // TODO!!! only for debugging catapult
                    if ( int(self->command) >= 23088 )       // telnet localhost <rpc_port>
                    {
                        __LOG( "dbg self->command: " << int(self->command) );
                        if ( auto server = self->m_server.lock(); server )
                        {
                            server->dbgEmulateSignal( int(self->command) - 23088 ); // 0Z,1Z,2Z,3Z... ( 0Z==0, 1Z==1, 2Z==2 ... )
                        }
                        return;
                    }
                    
                    __LOG( "self->command: " << CMD_STR(self->command) );
                    asio::async_read( self->socket, asio::buffer( &self->packetLen, sizeof(self->packetLen) ),
                                              asio::transfer_exactly( sizeof(self->packetLen) ),
                                              [self = self] ( boost::system::error_code error, std::size_t bytes_transferred )
                    {
                        if ( !error )
                        {
                            if ( self->command == RPC_CMD::UP_CHANNEL_INIT )
                            {
                                if ( auto server = self->m_server.lock(); server )
                                {
                                    server->setUpSession( self->weak_from_this() );
                                }
                                self->readNextCommand();
                            }
                            else if ( self->command == RPC_CMD::DOWN_CHANNEL_INIT )
                            {
                                if ( auto server = self->m_server.lock(); server )
                                {
                                    server->setDnSession( self->weak_from_this() );
                                }
                            }
                            else if ( self->command == RPC_CMD::log )
                            {
                                self->readLog( false );
                            }
                            else if ( self->command == RPC_CMD::log_err )
                            {
                                self->readLog( true );
                            }
                        }
                    });
                }
            });
        }
        
        void readNextCommand()
        {
            asio::async_read( socket, asio::buffer( &command, sizeof(command) ),
                                      asio::transfer_exactly( sizeof(command) ),
                                      [w = weak_from_this()] ( boost::system::error_code error, std::size_t bytes_transferred )
            {
                if ( auto self = w.lock(); self && !error )
                {
                    __LOG( "readCommand::self->command: " << CMD_STR(self->command) );
                    asio::async_read( self->socket, asio::buffer( &self->packetLen, sizeof(self->packetLen) ),
                                              asio::transfer_exactly( sizeof(self->packetLen) ),
                                              [w = w] ( boost::system::error_code error, std::size_t bytes_transferred )
                    {
                        if ( auto self = w.lock(); self && !error )
                        {
//                            __LOG( "self->packetLen: " << self->packetLen );

                            if ( self->command == RPC_CMD::UP_CHANNEL_INIT )
                            {
                                __LOG_WARN( "ignore unexpected command: RPC_CMD::UP_CHANNEL_INIT" );
                            }
                            else if ( self->command == RPC_CMD::DOWN_CHANNEL_INIT )
                            {
                                __LOG_WARN( "ignore unexpected command: RPC_CMD::DOWN_CHANNEL_INIT" );
                            }
                            else if ( self->command == RPC_CMD::PING )
                            {
                                assert( self->packetLen == 0 );
                                //__LOG_WARN( "ignore unexpected command: RPC_CMD::PING" );
                            }
                            else
                            {
                                if ( self->packetLen == 0 )
                                {
                                    if ( auto server = self->m_server.lock(); server )
                                    {
                                        server->handleCommand( self->command, nullptr );
                                    }
                                    self->sendAck();
                                }
                                else
                                {
                                    self->streambuf.prepare( self->packetLen );

                                    asio::async_read( self->socket, self->streambuf,
                                                                    asio::transfer_exactly( self->packetLen ),
                                                                    [self = self] ( boost::system::error_code error, std::size_t bytes_transferred )
                                    {
                                        if ( ! error && bytes_transferred == self->packetLen )
                                        {

//                                            __LOG( "self->packet_len: " << self->packetLen )
//                                            __LOG( "self->streambuf.size: " << self->streambuf.size() )

                                            //self->streambuf.commit(bytes_transferred);
                                            std::istream is( &self->streambuf );
                                            {
                                                cereal::PortableBinaryInputArchive iarchive(is);
                                                if ( auto server = self->m_server.lock(); server )
                                                {
                                                    server->handleCommand( self->command, &iarchive );
                                                }
                                            }
                                            self->streambuf.consume( bytes_transferred );
                                            self->sendAck();
                                        }
                                    });
                                }
                            }
                            
                            self->readNextCommand();
                        }
                    });
                }
            });
        }
        
        void readLog( bool isError )
        {
            streambuf.prepare( packetLen );

            asio::async_read( socket, streambuf,
                                        asio::transfer_exactly( packetLen ),
                                        [self = shared_from_this(),isError] ( boost::system::error_code error, std::size_t bytes_transferred )
            {
                if ( ! error )
                {
                    std::istream is( &self->streambuf );
                    if ( isError )
                    {
                        __LOG( "!Daemon Error: " << is.rdbuf() );
                    }
                    else
                    {
                        __LOG( "!Daemon log: " << is.rdbuf() );
                    }
                }
            });
        }
        
        void sendAck()
        {
            RPC_CMD command = RPC_CMD::ack;
            boost::system::error_code error;
            std::size_t bytes_transferred = asio::write( socket, asio::buffer( &command, sizeof(command) ), error );
            if ( error || bytes_transferred != sizeof(command) )
            {
                //TODO
                _LOG_ERR( "sendAck error: " << error )
            }

            uint16_t packetLen = 0;
            bytes_transferred = asio::write( socket, asio::buffer( &packetLen, sizeof(packetLen) ), error );
            if ( error || bytes_transferred != sizeof(packetLen) )
            {
                //TODO
                _LOG_ERR( "sendAck error (2): " << error )
            }
        }

        void send( RPC_CMD command, const std::string& parameters )
        {
            __LOG( "server send: " << CMD_STR(command) )
            boost::system::error_code error;
            std::size_t bytes_transferred = asio::write( socket, asio::buffer( &command, sizeof(command) ), error );
            if ( error || bytes_transferred != sizeof(command) )
            {
                //TODO
                _LOG_ERR( "send error: " << error )
            }

            uint16_t packetLen = (uint16_t) parameters.size();
            bytes_transferred = asio::write( socket, asio::buffer( &packetLen, sizeof(packetLen) ), error );
            if ( error || bytes_transferred != sizeof(packetLen) )
            {
                //TODO
                _LOG_ERR( "send error (2): " << error )
            }
                    
            if ( parameters.size() > 0 )
            {
                bytes_transferred = asio::write( socket, asio::buffer( parameters.c_str(), parameters.size() ), error );
//                __LOG( "parameters.size(): " << parameters.size() )
//                __LOG( "bytes_transferred" << bytes_transferred )
                if ( error || bytes_transferred != parameters.size() )
                {
                    //TODO
                    _LOG_ERR( "send error (3): " << error )
                }
            }

        }
        
        void readAck()
        {
            RPC_CMD command;
            boost::system::error_code ec;
            
        readAgain:
            asio::read( socket,
                       asio::buffer( &command, sizeof(command) ),
                       asio::transfer_exactly( sizeof(command) ),
                       ec );
            if ( ec )
            {
                //TODO
                __LOG( "send error: " << ec )
            }

            uint16_t packetLen;
            asio::read( socket,
                       asio::buffer( &packetLen, sizeof(packetLen) ),
                       asio::transfer_exactly( sizeof(packetLen) ),
                       ec );
            if ( ec )
            {
                //TODO
                _LOG_ERR( "send error: " << ec )
            }
            if ( command == RPC_CMD::PING )
                goto readAgain;
            
            __ASSERT( command == RPC_CMD::ack && packetLen == 0 );
        }

        std::array<uint8_t,32> readAckDbgGetRootHash()
        {
            RPC_CMD command;
            boost::system::error_code ec;
            
        readAgain:
            asio::read( socket,
                       asio::buffer( &command, sizeof(command) ),
                       asio::transfer_exactly( sizeof(command) ),
                       ec );
            if ( ec )
            {
                //TODO
                __LOG( "send error: " << ec )
            }

            uint16_t packetLen;
            asio::read( socket,
                       asio::buffer( &packetLen, sizeof(packetLen) ),
                       asio::transfer_exactly( sizeof(packetLen) ),
                       ec );
            if ( ec )
            {
                //TODO
                _LOG_ERR( "send error: " << ec )
            }
            if ( command == RPC_CMD::PING )
                goto readAgain;

            std::array<uint8_t,32> hash;
            __ASSERT( command == RPC_CMD::dbgHash && packetLen == 32 );

            asio::read( socket,
                       asio::buffer( hash.data(), 32 ),
                       asio::transfer_exactly( 32 ),
                       ec );
            if ( ec )
            {
                //TODO
                _LOG_ERR( "send error: " << ec )
            }
            
            return hash;
        }
    };

private:

    void closeSockets()
    {
        __LOG( "Start Close Sockets" )
        m_socket->close();
        __LOG( "Finish Close Sockets" )
    }
    
    void async_accept()
    {
        m_socket.emplace( m_context );

        assert( weak_from_this().lock());
        
        m_acceptor->async_accept( *m_socket, [weak_ptr=weak_from_this()] (boost::system::error_code ec)
        {
            if ( auto self = weak_ptr.lock(); self )
            {
                if ( ec )
                {
                    if ( ec.value() == boost::system::errc::operation_canceled )
                    {
						self->m_isConnectionLost = true;
                        self->handleConnectionLost();
                    }
                    else
                    {
                        _LOG_ERR( "error in RpcTcpServer::async_accept : " << ec.message() )
                    }
                }
                else
                {
                    __LOG( "accepted" )
                    std::make_shared<Session>( std::move(*self->m_socket), self->weak_from_this() )->start();
                    self->async_accept();
                }
            }
        });
    }

    void setUpSession( std::weak_ptr<Session> s )
    {
        if ( m_upSession )
        {
            __LOG_WARN( "double setUpSession()" );
            return;
        }

        __LOG( "setUpSession()" );
        m_upSession = s.lock();
        
        if ( m_upSession && m_dnSession )
        {
            initiateRemoteService();
        }
    }
    
    void setDnSession( std::weak_ptr<Session> s )
    {
        if ( m_dnSession )
        {
            __LOG_WARN( "double setDnSession()" );
            return;
        }

        __LOG( "setDnSession()" );
        m_dnSession = s.lock();

        if ( m_upSession && m_dnSession )
        {
            initiateRemoteService();
        }
    }
};
    
void RpcTcpServer::rpcCall( RPC_CMD func, const std::string& parameters )
{
    if ( m_isConnectionLost ) {
		return;
	}
	m_dnSession->send( func, parameters );
    __LOG( "*rpc* rpcCall: " << CMD_STR(func) )
    m_dnSession->readAck();
    __LOG(  "Read Ack" );
}

std::array<uint8_t,32> RpcTcpServer::rpcDbgGetRootHash( const std::string& parameters )
{
    m_dnSession->send( RPC_CMD::dbgGetRootHash, parameters );
    __LOG( "*rpc* rpcDbgGetRootHash: " )
    auto hash = m_dnSession->readAckDbgGetRootHash();
    m_dnSession->readAck();
    return hash;
}



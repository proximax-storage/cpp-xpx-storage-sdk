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

using CallHandler = std::function< void( const std::string& functionName, std::ostringstream& parameters ) >;

class RpcTcpServer
{
    class Session;
    
    class Session : public std::enable_shared_from_this<Session>
    {
        RpcTcpServer&           m_server;
        
        asio::ip::tcp::socket   socket;
        asio::streambuf         streambuf;
        std::vector<uint8_t>    in_buff;

        uint16_t                packetLen;
        RPC_CMD                 command;

    public:

        Session( asio::ip::tcp::socket&& socket, RpcTcpServer& server )
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
                    __LOG( "self->command: " << CMD_STR(self->command) );
                    asio::async_read( self->socket, asio::buffer( &self->packetLen, sizeof(self->packetLen) ),
                                              asio::transfer_exactly( sizeof(self->packetLen) ),
                                              [self = self] ( boost::system::error_code error, std::size_t bytes_transferred )
                    {
                        __LOG( "self->packetLen: " << self->packetLen );
                        if ( !error )
                        {
                            if ( self->command == RPC_CMD::UP_CHANNEL_INIT )
                            {
                                self->m_server.setUpSession( self->weak_from_this() );
                            }
                            else if ( self->command == RPC_CMD::DOWN_CHANNEL_INIT )
                            {
                                self->m_server.setDnSession( self->weak_from_this() );
                            }
                            
                            self->readNextCommand();
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
                            __LOG( "self->packetLen: " << self->packetLen );

                            if ( self->command == RPC_CMD::UP_CHANNEL_INIT )
                            {
                                self->m_server.setUpSession( self->weak_from_this() );
                            }
                            else if ( self->command == RPC_CMD::DOWN_CHANNEL_INIT )
                            {
                                self->m_server.setDnSession( self->weak_from_this() );
                            }
                            else if ( self->command == RPC_CMD::PING )
                            {
                                abort();
                            }
                            else
                            {
                                if ( self->streambuf.size() < self->packetLen )
                                {
                                    self->streambuf.prepare( self->packetLen );
                                }

                                asio::async_read( self->socket, self->streambuf,
                                                                asio::transfer_exactly( self->packetLen ),
                                                                [self = self] ( boost::system::error_code error, std::size_t bytes_transferred )
                                {
                                    if ( ! error && bytes_transferred == self->packetLen )
                                    {

                                        __LOG( "self->packet_len: " << self->packetLen )
                                        __LOG( "self->streambuf.size: " << self->streambuf.size() )

                                        //self->streambuf.commit(bytes_transferred);
                                        std::istream is( &self->streambuf );
                                        {
                                            cereal::PortableBinaryInputArchive iarchive(is);
                                            self->handleCommand( self->command, iarchive );
                                        }
                                        self->streambuf.consume( bytes_transferred );
                                    }
                                });
                            }
                            
                            self->readNextCommand();
                        }
                    });
                }
            });
        }

        void handleCommand( RPC_CMD command, cereal::PortableBinaryInputArchive& parameters )
        {
            
        }
        
        void send( RPC_CMD command, std::string&& parameters )
        {
            __LOG( "server send: " << CMD_STR(command) )
            boost::system::error_code error;
            std::size_t bytes_transferred = asio::write( socket, asio::buffer( &command, sizeof(command) ), error );
            if ( error || bytes_transferred != sizeof(command) )
            {
                //TODO
                __LOG( "send error: " << error )
            }

            uint16_t packetLen = (uint16_t) parameters.size();
            bytes_transferred = asio::write( socket, asio::buffer( &packetLen, sizeof(packetLen) ), error );
            if ( error || bytes_transferred != sizeof(packetLen) )
            {
                //TODO
                __LOG( "send error (2): " << error )
            }
                    
            if ( parameters.size() > 0 )
            {
                bytes_transferred = asio::write( socket, asio::buffer( parameters.data(), sizeof(parameters.size()) ), error );
                if ( error || bytes_transferred != sizeof(parameters.size()) )
                {
                    //TODO
                    __LOG( "send error (3): " << error )
                }
            }

        }
        
        void async_send( RPC_CMD command, std::string&& parameters )
        {
            __LOG( "server send: " << CMD_STR(command) )
            asio::async_write( socket,
                               asio::buffer( &command, sizeof(command) ),
                               [self = shared_from_this(),parameters=std::move(parameters)] ( std::error_code error, std::size_t bytes_transferred )
            {
                if ( error )
                {
                    //TODO
                    __LOG( "send error: " << error )
                }
                uint16_t packetLen = (uint16_t) parameters.size();
                asio::async_write( self->socket,
                                   asio::buffer( &packetLen, sizeof(packetLen) ),
                                  [self=self,parameters=std::move(parameters)] ( std::error_code error, std::size_t bytes_transferred )
                {
                    if ( error )
                    {
                        //TODO
                        __LOG( "send error (2): " << error )
                    }
                    
                    if ( parameters.size() > 0 )
                    {
                        asio::async_write( self->socket,
                                           asio::buffer( parameters.data(), sizeof(parameters.size()) ),
                                          [self=self,parameters=std::move(parameters)] ( std::error_code error, std::size_t bytes_transferred )
                        {
                            if ( error )
                            {
                                //TODO
                                __LOG( "send error (3): " << error )
                            }
                        });
                    }
                });
            });

        }
        
        void readAnswer()
        {
            RPC_CMD command;
            boost::system::error_code ec;
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
            assert( packetLen == 0 );
            if ( ec )
            {
                //TODO
                __LOG( "send error: " << ec )
            }
        }
    };

private:

    asio::io_context&                    m_context;
    asio::ip::tcp::acceptor              m_acceptor;
    std::optional<asio::ip::tcp::socket> m_socket;
    std::shared_ptr<Session>             m_upSession;
    std::shared_ptr<Session>             m_dnSession;
    
    void async_accept()
    {
        m_socket.emplace( m_context );

        m_acceptor.async_accept( *m_socket, [&] (boost::system::error_code error)
        {
            __LOG( "accepted" )
            std::make_shared<Session>( std::move(*m_socket), *this )->start();
            async_accept();
        });
    }

    void setUpSession( std::weak_ptr<Session> s )
    {
        __LOG( "setUpSession()" );
        m_upSession = s.lock();
        
        if ( m_upSession && m_dnSession )
        {
            m_dnSession->send( RPC_CMD::READY_TO_USE, "" );
        }
    }
    
    void setDnSession( std::weak_ptr<Session> s )
    {
        __LOG( "setDnSession()" );
        m_dnSession = s.lock();

        if ( m_upSession && m_dnSession )
        {
            m_dnSession->send( RPC_CMD::READY_TO_USE, "" );
        }
    }
    
public:
    
    RpcTcpServer( asio::io_context& io_context, std::string address, std::uint16_t port )
        : m_context(io_context)
        , m_acceptor( m_context, asio::ip::tcp::endpoint( boost::asio::ip::address::from_string(address.c_str()), port ))
    {
        __LOG( "RpcTcpServer()" )

        async_accept();
    }
    
    virtual ~RpcTcpServer()
    {
        closeSockets();
    }
    
    void closeSockets()
    {
        m_socket->close();
    }
    
    void rpcCall( RPC_CMD func, std::string&& parameters )
    {
        m_dnSession->send( func, std::move(parameters) );
    }
    
    void readAnswer()
    {
        m_dnSession->readAnswer();
    }
    
    virtual void handleCommand( RPC_CMD command, cereal::PortableBinaryInputArchive& parameters ) = 0;
};
    


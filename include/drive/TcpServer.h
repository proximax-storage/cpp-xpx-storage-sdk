/*
*** Copyright 2024 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include <iostream>
#include <ostream>
#include <strstream>

#include <boost/asio.hpp>

#include <cereal/types/vector.hpp>
#include <cereal/types/array.hpp>
#include <cereal/types/map.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/types/string.hpp>
#include <cereal/archives/binary.hpp>

#include "TcpConnectionProtocol.h"

namespace sirius { namespace drive {

class TcpClientSession;
class TcpServer;

class ITcpServer
{
public:
    virtual ~ITcpServer() = default;
    
    virtual void onRequestReceived( uint8_t* data, size_t dataSize, std::weak_ptr<TcpClientSession> session ) = 0;
};

class TcpClientSession: public std::enable_shared_from_this<TcpClientSession>
{
protected:
    boost::asio::ip::tcp::socket m_socket;
    ITcpServer&                  m_server;
    
//    boost::asio::steady_timer    m_timer;
    
    uint16_t                     m_dataLength;
    std::vector<uint8_t>         m_packetData;
    
public:
    std::string             m_dbgOurPeerName;

public:
    TcpClientSession( boost::asio::ip::tcp::socket&& socket, ITcpServer& server )
     : m_socket( std::move(socket) )
     , m_server(server)
     //, m_timer( m_socket.get_executor() )
    {
    }
    
    ~TcpClientSession()
    {
        _LOG( "~TcpClientSession" )
    }
    
    template<class PacketT>
    void sendReply( TcpResponseId replayId, const PacketT& replay )
    {
        auto os = std::make_unique<std::ostringstream>( std::ios::binary );
        cereal::BinaryOutputArchive archive( *os );
        archive( uint16_t{} );
        archive( uint16_t(replayId) );
        archive( replay );
        
        assert( os->rdbuf()->view().size() > 2 );
        assert( os->rdbuf()->view().size() < 0xFFFF );
        
        uint16_t size = (uint16_t) os->rdbuf()->view().size();
        _LOG( "#TcpClientSession: sendReply: " << size );
        
        if ( size == 0 )
        {
            // no reply
            return;
        }
        
        auto buffer = (uint8_t*)os->rdbuf()->view().data();
        buffer[0] = size&0x00FF;
        buffer[1] = (size&0xFF00) >> 8;
        
        uint8_t* message = new uint8_t[size];
        std::memcpy( message, buffer, size );
        
        m_socket.async_send( boost::asio::buffer( message, size ),
                            [self=this->shared_from_this(),message] ( auto error, auto sentSize )
        {
            __LOG( "#TcpClientSession: async_send ended" );

            delete [] message;
            
            if (error)
            {
                __LOG( "#TcpClientSession: async_send error(2): " << error.message() );
                _LOG_ERR( "#TcpClientSession: async_send error(2): " << error.message() );
            }
            __LOG( "#TcpClientSession: sendReply sent" );
        });

    //        m_socket.async_send( boost::asio::buffer( os->rdbuf()->view().data(), size ),
    //                            [self=this->shared_from_this(),os=std::move(os)] ( auto error, auto sentSize )
    //        {
    //            if (error)
    //            {
    //                __LOG( "#TcpClientSession: async_send error(2): " << error.message() );
    //                _LOG_ERR( "#TcpClientSession: async_send error(2): " << error.message() );
    //            }
    //            __LOG( "#TcpClientSession: sendReply sent" );
    //        });
    }
    
private:
    friend TcpServer;
    
    void readPacketHeader()
    {
//        m_timer.expires_after( std::chrono::seconds(5) );
//        m_timer.async_wait( [self = shared_from_this()] (const boost::system::error_code& ec)
//        {
//            if ( ec == boost::asio::error::operation_aborted )
//            {
//                __LOG( "#TcpClientSession: timer was cancelled" );
//            }
//            else if ( ec )
//            {
//                __LOG( "#TcpClientSession: timer error: " << ec.message() );
//            }
//            else
//            {
//                __LOG( "#TcpClientSession: close socket: " << ec.message() );
//                self->m_socket.close();
//            }
//        });
        
        boost::asio::async_read( m_socket, boost::asio::buffer(&m_dataLength, sizeof(m_dataLength)), [self=this->shared_from_this()] ( auto error, auto bytes_transferred )
        {
            self -> doReadPacketHeader( error, bytes_transferred );
        });
    }
    
    void doReadPacketHeader( boost::system::error_code error, size_t bytes_transferred )
    {
        if ( error )
        {
            if ( error == boost::asio::error::eof )
            {
                _LOG("#TcpClientSession: Connection closed");
                return;
            }
            _LOG_ERR( "#TcpClientSession read error: " << error.message() );
            //connectionLost( error );
            return;
        }
        if ( bytes_transferred != sizeof(m_dataLength) )
        {
            _LOG_ERR( "#TcpClientSession read error (m_dataLength): " << bytes_transferred << " vs " << sizeof(m_dataLength) );
            //connectionLost( error );
            return;
        }
        
        m_dataLength -= sizeof(m_dataLength);
        _LOG( "#TcpClientSession received: " << m_dataLength );
        
        m_packetData.resize( m_dataLength );
        boost::asio::async_read( m_socket, boost::asio::buffer(m_packetData.data(), m_dataLength ),
                                [self=this->shared_from_this()] ( auto error, auto bytes_transferred )
        {
            self -> readPacketData( error, bytes_transferred );
        });
    }
    
    void readPacketData( boost::system::error_code error, size_t bytes_transferred )
    {
        if ( error )
        {
            _LOG_ERR( "#TcpClientSession read error: " << error.message() );
            //connectionLost( error );
            return;
        }
        if ( bytes_transferred != m_dataLength )
        {
            _LOG_ERR( "#TcpClientSession read error (bytes_transferred): " << bytes_transferred << " vs "  << m_dataLength );
            //connectionLost( error );
            return;
        }
        
        _LOG( "#TcpClientSession: readPacketData: " << bytes_transferred )
        m_server.onRequestReceived( m_packetData.data(), m_dataLength, weak_from_this() );
        
        //m_timer.cancel();
        readPacketHeader();
    }
};


class TcpServer: public ITcpServer
{
    boost::asio::io_context*                        m_context;
    boost::asio::ip::tcp::endpoint                  m_endpoint;
    std::optional<boost::asio::ip::tcp::acceptor>   m_acceptor;
    
public:
    
    // stube for client
    TcpServer() {}
    
    TcpServer( boost::asio::io_context& context,
              const std::string&       addr,
              const std::string&       port )
    :
        m_context( &context )
    {
        try
        {
            boost::asio::ip::tcp::resolver resolver(*m_context);
            m_endpoint = *resolver.resolve( addr, port ).begin();
            
            m_acceptor = boost::asio::ip::tcp::acceptor( *m_context, m_endpoint );
            
            asyncAccept();
        }
        catch( std::runtime_error& e )
        {
            __LOG("#TcpServer exception: " << e.what() )
            __LOG("??? Port already in use ???" )
        }
    }
    
    void asyncAccept()
    {
        m_acceptor->async_accept( [this]( boost::system::error_code ec, boost::asio::ip::tcp::socket socket )
        {
            if (ec)
            {
                _LOG_ERR( "async_accept error: " << ec.message() );
            }
            else
            {
                boost::asio::socket_base::keep_alive option(true);
                socket.set_option(option);
                
                auto session = createSession( std::move(socket) );
                session->readPacketHeader();
                asyncAccept();
            }
        });
    }
    
    std::shared_ptr<TcpClientSession> createSession( boost::asio::ip::tcp::socket&& socket )
    {
        return std::make_shared< TcpClientSession>( std::move(socket), *this );
    }
    
    struct Buffer : public std::streambuf
    {
        Buffer( char* data, size_t dataSize ) { setg( data, data, data + dataSize ); }
    };
    
    void onRequestReceived( uint8_t* data, size_t dataSize, std::weak_ptr<TcpClientSession> session ) override
    {
        // !!! ONLY for debugging
        // (it must be overriden)
        
        Buffer streambuf{ (char*)data, dataSize };
        std::istream is(&streambuf);
        
        cereal::BinaryInputArchive iarchive( is );
        
        uint16_t requestId;
        iarchive( requestId );
        __LOG( "requestId: " << requestId );
        
        std::string request;
        iarchive( request );
        __LOG( "request: " << request );
        
        //sleep(100);
        
        if ( auto tcpSession = session.lock() )
        {
            tcpSession->sendReply( TcpResponseId::peer_ip_response, std::string("<dbg-replay>") );
        }
    }
};

}}

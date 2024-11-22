/*
*** Copyright 2024 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include <boost/asio.hpp>
#include <boost/system/error_code.hpp>
#include <iostream>
#include <memory>

using boost::asio::ip::tcp;
using boost::asio::io_context;

namespace sirius { namespace drive {

class TcpClient;

class IClientConnectionManager
{
public:
    virtual ~IClientConnectionManager() = default;

    virtual void onResponseReceived( bool success, uint8_t* data, size_t dataSize, TcpClient& connection ) = 0;
};


class TcpClient : public std::enable_shared_from_this<TcpClient>
{
    IClientConnectionManager& m_manager;
    io_context&     m_context;
    tcp::socket     m_socket;
    
    std::string     m_sendData;
    
    uint16_t                m_dataLength;
    std::vector<uint8_t>    m_packetData;
    
    Timer                   m_timer;
    
public:
    std::string             m_dbgOurPeerName;
    
public:
    TcpClient( IClientConnectionManager& manager, io_context& context )
    :
    m_manager(manager),
    m_context(context),
    m_socket(context)
    {
    }
    
    ~TcpClient()
    {
        m_timer.cancel();
        
        try
        {
            m_socket.close();
        }
        catch (...) {
            _LOG( "~TcpClient: exception" )
        }
        
        _LOG( "~TcpClient" )
    }
    
    void sendRequestTo( boost::asio::ip::tcp::endpoint endpoint, std::string&& data )
    {
        m_sendData = std::move(data);
        assert( m_sendData.size() <= 0xFFFFF );
        ((uint8_t&) m_sendData[0]) = m_sendData.size() & 0xFF;
        ((uint8_t&) m_sendData[1]) = (m_sendData.size() & 0xFF00) << 8;
        
        // Response Time-out: 5 seconds
        m_timer = Timer { m_context, 15*1000, [self = this->weak_from_this()]
        {
            if ( auto connectionPtr = self.lock(); connectionPtr )
            {
                connectionPtr->m_manager.onResponseReceived( false, nullptr, 0, *connectionPtr.get() );
            }
        }};
        
        m_socket.async_connect( endpoint, [endpoint, self = this->weak_from_this()] ( const boost::system::error_code& ec )
        {
            if ( auto connectionPtr = self.lock(); connectionPtr )
            {
                if ( ec )
                {
                    __LOG( "Connection error: " << ec.message() << "; to: " << endpoint );
                    connectionPtr->m_manager.onResponseReceived( false, nullptr, 0, *connectionPtr.get() );
                }
                else
                {
                    connectionPtr->write( connectionPtr->m_sendData.c_str(), connectionPtr->m_sendData.size() );
                }
            }
        });
    }
    
private:
    void write( const char* message, size_t size )
    {
        boost::asio::async_write( m_socket, boost::asio::buffer(message,size),
                                 [self = this->weak_from_this()] ( const boost::system::error_code& ec,
                                                                  std::size_t                      length )
        {
            if ( auto connectionPtr = self.lock(); connectionPtr )
            {
                if ( ec )
                {
                    __LOG( "async write error: " << ec.message() << "; to: " << connectionPtr->m_socket.remote_endpoint() );
                    connectionPtr->m_manager.onResponseReceived( false, nullptr, 0, *connectionPtr.get() );
                }
                else
                {
                    __LOG( " Client sent message: " << length << " bytes" );
                    connectionPtr->readPacketHeader();
                }
            }
        });
    }
    
    
    void readPacketHeader()
    {
        boost::asio::async_read( m_socket, boost::asio::buffer( &m_dataLength, sizeof(m_dataLength) ), [self=this->shared_from_this()] ( auto ec, auto bytes_transferred )
        {
            if ( ec )
            {
                __LOG( "async read error: " << ec.message() << "; to: " );//<< self->m_socket.remote_endpoint() );
                self->m_manager.onResponseReceived( false, nullptr, 0, *self.get() );
                return;
            }
            
            self->readPacketData( bytes_transferred );
        });
    }
    
    void readPacketData( size_t bytes_transferred )
    {
        if ( bytes_transferred != sizeof(m_dataLength) )
        {
            _LOG_ERR( "TcpClient read error (m_dataLength): " << bytes_transferred << " vs " << sizeof(m_dataLength) );
            m_manager.onResponseReceived( false, nullptr, 0, *this );
            return;
        }
        
        // tail lenght is packet_len-2
        m_dataLength -= sizeof(m_dataLength);
        
        _LOG( "TcpClient received packet size: " << m_dataLength );
        
        if ( m_dataLength == 0 || m_dataLength > 16*1024 )
        {
            _LOG_ERR( "TcpClient invalid dataLength: " << m_dataLength );
            m_manager.onResponseReceived( false, nullptr, 0, *this );
            return;
        }
        
        
        m_packetData.resize( m_dataLength-2 );
        boost::asio::async_read( m_socket, boost::asio::buffer(m_packetData.data(), m_dataLength),
                                [self=this->shared_from_this()] ( auto ec, auto bytes_transferred )
        {
            if ( ec )
            {
                __LOG( "async read error (2): " << ec.message() << "; to: " << self->m_socket.remote_endpoint() );
                self->m_manager.onResponseReceived( false, nullptr, 0, *self.get() );
                return;
            }
            self->onPacketData( bytes_transferred );
        });
    }
    
    void onPacketData( size_t bytes_transferred )
    {
        if ( bytes_transferred != m_dataLength )
        {
            _LOG_ERR( "TcpClient read error (bytes_transferred): " << bytes_transferred << " vs "  << m_dataLength );
            m_manager.onResponseReceived( false, nullptr, 0, *this );
            return;
        }
        
        m_manager.onResponseReceived( true, m_packetData.data(), bytes_transferred, *this );
        
        // automatically close connection (by shared pointer)
    }
};

}}

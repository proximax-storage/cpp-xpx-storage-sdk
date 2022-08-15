//
//  RpcClient.h
//  SyncRpc
//
//  Created by Aleksander Tsarenko on 13.07.22.
//

#include "RpcReplicatorCommands.h"

#include <syslog.h>
#include <thread>

#include <boost/asio/connect.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio.hpp>

#include <boost/lambda/bind.hpp>
#include <boost/lambda/lambda.hpp>

#include <cereal/types/string.hpp>
#include <cereal/types/array.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/map.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/archives/portable_binary.hpp>

#include <iostream>
#include <fstream>

// only for debugging libtorrent asserts
#include "libtorrent/bdecode.hpp"
#include "libtorrent/entry.hpp"


#   define RPC_LOG(expr) __LOG( "*RPC* " << expr)

#   define RPC_ERR(expr) { \
        std::cout << __FILE__ << ":" << __LINE__ << ": " << __FUNCTION__ << ": "<< expr << "\n" << std::flush; \
        exit(0); \
}


namespace asio = boost::asio;
using     tcp  = boost::asio::ip::tcp;


class RpcTcpSocketBase
{
protected:
    asio::io_context        m_context;
    tcp::socket             m_socket;

    RpcTcpSocketBase() : m_socket(m_context)
    {
    }
    
    ~RpcTcpSocketBase()
    {
        m_socket.close();
    }
    
public:
    
    void connect( std::string address, std::string port )
    {
        RPC_LOG( "try to connect: " << address << ", " << port )

        tcp::resolver resolver( m_context );
        auto iter =  resolver.resolve( address, port );

#ifdef DEBUG_NO_DAEMON_REPLICATOR_SERVICE
        RPC_LOG( "DEBUG_NO_DAEMON_REPLICATOR_SERVICE" )

        boost::system::error_code ec = boost::asio::error::would_block;

        while ( ec == boost::asio::error::would_block || ec == boost::asio::error::connection_refused )
        {
            boost::asio::connect( m_socket, iter, ec );

            //RPC_LOG( "ec: " << ec << " " << boost::asio::error::connection_refused )
            if ( !ec )
            {
                break;
            }
            usleep(10000);
        }
        RPC_LOG( "connect ec: " << ec )
#else
        boost::system::error_code ec = boost::asio::error::would_block;
        boost::asio::async_connect( m_socket, iter, boost::lambda::var(ec) = boost::lambda::_1);

        while ( ec == boost::asio::error::would_block )
        {
            m_context.run_one();
        }
#endif

        if ( ec || !m_socket.is_open() )
        {
            RPC_LOG( "connect failed; ec: " << ec )
            exit(0);
            //throw boost::system::system_error( ec ? ec : boost::asio::error::operation_aborted );
        }
        RPC_LOG( "connected: " << address << ", " << port )
    }
    
    void sendCommandWoAck( RPC_CMD command )
    {
        boost::system::error_code ec = boost::asio::error::would_block;
        asio::write( m_socket, asio::buffer( &command, sizeof(command) ), ec );
        if (ec)
        {
            RPC_ERR( "!write error!: " << ec.message() )
            exit(0);
        }

        uint16_t packetLen = 0;
        boost::asio::write( m_socket, asio::buffer( &packetLen, sizeof(packetLen) ), ec );

        if (ec)
        {
            RPC_ERR( "!write error!: " << ec.message() )
            exit(0);
        }

        if ( command != RPC_CMD::PING )//&& command != RPC_CMD::ack  )
        {
            RPC_LOG( "sendCommandWoAck: " << (void*)this << " " << CMD_STR(command) )
            RPC_LOG( "" )
        }
    }
    
    void readAck()
    {
        RPC_CMD command;
        boost::system::error_code ec;
        asio::read( m_socket,
                   asio::buffer( &command, sizeof(command) ),
                   asio::transfer_exactly( sizeof(command) ),
                   ec );
        if ( ec )
        {
            _LOG_ERR( "readAck() error: " << ec )
            exit(0);
        }

        uint16_t packetLen;
        asio::read( m_socket,
                   asio::buffer( &packetLen, sizeof(packetLen) ),
                   asio::transfer_exactly( sizeof(packetLen) ),
                   ec );
        if ( ec )
        {
            _LOG_ERR( "readAck() error: " << ec )
            exit(0);
        }

        assert( command == RPC_CMD::ack && packetLen == 0 );
    }
};

// From remote to chain
//
class UpRpcTcpSocket : public RpcTcpSocketBase
{
public:
    UpRpcTcpSocket()
    {
    }
    
    void sendCommand( RPC_CMD command, const std::string& parameters )
    {
        RPC_LOG( "->> sendCommand: " << CMD_STR(command) << " parameters.size=" << parameters.size() )
        RPC_LOG( "" )

        boost::system::error_code ec = boost::asio::error::would_block;
        asio::write( m_socket, asio::buffer( &command, sizeof(command) ), ec );
        if (ec)
        {
            RPC_ERR( "!write error!: " << ec.message() )
            exit(0);
        }

        uint16_t packetLen = (uint16_t)parameters.size();
        boost::asio::write( m_socket, asio::buffer( &packetLen, sizeof(packetLen) ), ec );

        if (ec)
        {
            RPC_LOG( "!write error! (2): " << ec.message() )
            exit(0);
        }

        boost::asio::write( m_socket, asio::buffer( parameters.c_str(), parameters.size() ), ec );

        if (ec)
        {
            RPC_LOG( "!write error! (3): " << ec.message() )
            exit(0);
        }

        readAck();
        
        //RPC_LOG( "sendCommand(2) done: " << CMD_STR(command) )
    }
};


// From chain to remote
//
class DnRpcTcpSocket : public RpcTcpSocketBase
{
public:
    
    DnRpcTcpSocket()
    {
    }
    
    RPC_CMD readCommand( asio::streambuf& streambuf )
    {
        //RPC_LOG( "readCommand(): started" )

        boost::system::error_code ec;

        RPC_CMD command;
        auto len = asio::read( m_socket, asio::buffer( &command, sizeof(command) ), ec );

        if( ec )
        {
            RPC_LOG( "read command error: ec: " << ec.message() )
            exit(0);
        }
        
        if ( len != sizeof(command) )
        {
            RPC_LOG( "bad len: " << len << " "  << sizeof(command) )
            exit(0);
        }
                              
//        RPC_LOG( "command: " << CMD_STR(command) )
        
        uint16_t packetLen;
        len = asio::read( m_socket, asio::buffer( &packetLen, sizeof(packetLen) ), asio::transfer_exactly( sizeof(packetLen) ), ec );

//        RPC_LOG( "packetLen: " << packetLen )

        if( ec )
        {
            RPC_LOG( "ec(2): " << ec.message() )
            exit(0);
        }
        
        if ( len != sizeof(packetLen) )
        {
            RPC_LOG( "bad len(2): " << len << " "  << sizeof(packetLen) )
            exit(0);
        }

        if ( packetLen > 0 )
        {
            streambuf.prepare( packetLen );

//            RPC_LOG( "child: read packet: " << packetLen )
            len = asio::read( m_socket, streambuf, asio::transfer_exactly( packetLen ), ec );
//            RPC_LOG( "child: packet received: " << packetLen )

            if( ec )
            {
                RPC_LOG( "ec(3): " << ec.message() )
                exit(0);
            }
            
            if ( len != packetLen )
            {
                RPC_LOG( "bad len(3): " << len << " "  << packetLen )
                exit(0);
            }
        }
        
        return command;
    }

    // for debugging
    void sendHashAnswer( RPC_CMD command, const std::array<uint8_t,32>& hash )
    {
        boost::system::error_code ec = boost::asio::error::would_block;
        asio::write( m_socket, asio::buffer( &command, sizeof(command) ), ec );
        if (ec)
        {
            RPC_ERR( "!write error!: " << ec.message() )
            exit(0);
        }

        uint16_t packetLen = 32;
        boost::asio::write( m_socket, asio::buffer( &packetLen, sizeof(packetLen) ), ec );

        if (ec)
        {
            RPC_ERR( "!write error!: " << ec.message() )
            exit(0);
        }

        boost::asio::write( m_socket, asio::buffer( hash.data(), 32 ), ec );

        if (ec)
        {
            RPC_ERR( "!write error!: " << ec.message() )
            exit(0);
        }

        RPC_LOG( "sendHashAnswer: " << (void*)this << " " << CMD_STR(command) )
        RPC_LOG( "" )
    }
};

class RpcClient
{
protected:
    UpRpcTcpSocket  m_upSocket; // from client to server
    DnRpcTcpSocket  m_dnSocket; // from server to client
    
    std::mutex      m_upMutex;

protected:

    RpcClient()
    {
    }
    
    virtual ~RpcClient() = default;
    
    virtual void handleCommand( RPC_CMD command, cereal::PortableBinaryInputArchive* parameters ) = 0;

    virtual void handleError( std::error_code ) = 0;

    virtual void handleConnectionLost() = 0;
    
public:
    
    void run( std::string address, std::string port )
    {
        boost::asio::streambuf inStreambuf;
        
        m_upSocket.connect( address, port );
        m_upSocket.sendCommandWoAck( RPC_CMD::UP_CHANNEL_INIT );

        std::thread( [this]
        {
            for(;;)
            {
                sleep(1);
                std::unique_lock<std::mutex> lock(m_upMutex);
                m_upSocket.sendCommandWoAck( RPC_CMD::PING );
            }
        }).detach();
        
        m_dnSocket.connect( address, port );
        m_dnSocket.sendCommandWoAck( RPC_CMD::DOWN_CHANNEL_INIT );
        
        for(;;)
        {
            RPC_CMD command = m_dnSocket.readCommand( inStreambuf );
            RPC_LOG( "<<- RpcClient.command: " << CMD_STR(command) )
            RPC_LOG( "" )

            switch (command)
            {
                case RPC_CMD::PING:
                {
                    //m_upSocket.sendCommand( RPC_CMD::dbgCrash, "" );
                    break;
                }
                default:
                {
                    if ( inStreambuf.size() > 0 )
                    {
                        std::istream is( &inStreambuf );
                        cereal::PortableBinaryInputArchive iarchive(is);
                        handleCommand( command, &iarchive );
                        inStreambuf.consume( inStreambuf.size() );
                    }
                    else
                    {
                        handleCommand( command, nullptr );
                    }
                    break;
                }
            };
        }
    }
    
    void sendAck()
    {
        m_dnSocket.sendCommandWoAck( RPC_CMD::ack );
    }
    
    void rpcCall( RPC_CMD func )
    {
        std::unique_lock<std::mutex> lock(m_upMutex);
        m_upSocket.sendCommand( func, "" );
    }

    template<class T>
    void rpcCall( RPC_CMD func, const T& p )
    {
        std::ostringstream os( std::ios::binary );
        cereal::PortableBinaryOutputArchive archive( os );
        archive( p );

        std::unique_lock<std::mutex> lock(m_upMutex);
        m_upSocket.sendCommand( func, os.str() );
    }
    
    template<class T,class T2>
    void rpcCall( RPC_CMD func, const T& p, const T2& p2 )
    {
        std::ostringstream os( std::ios::binary );
        cereal::PortableBinaryOutputArchive archive( os );
        archive( p );
        archive( p2 );

        std::unique_lock<std::mutex> lock(m_upMutex);
        m_upSocket.sendCommand( func, os.str() );
    }

    template<class T,class T2,class T3>
    void rpcCall( RPC_CMD func, const T& p, const T2& p2, const T3& p3 )
    {
        std::ostringstream os( std::ios::binary );
        cereal::PortableBinaryOutputArchive archive( os );
        archive( p );
        archive( p2 );
        archive( p3 );

        std::unique_lock<std::mutex> lock(m_upMutex);
        m_upSocket.sendCommand( func, os.str() );
    }

    template<class T,class T2,class T3,class T4>
    void rpcCall( RPC_CMD func, const T& p, const T2& p2, const T3& p3, const T4& p4 )
    {
        std::ostringstream os( std::ios::binary );
        cereal::PortableBinaryOutputArchive archive( os );
        archive( p );
        archive( p2 );
        archive( p3 );
        archive( p4 );

        std::unique_lock<std::mutex> lock(m_upMutex);
        m_upSocket.sendCommand( func, os.str() );
    }
};

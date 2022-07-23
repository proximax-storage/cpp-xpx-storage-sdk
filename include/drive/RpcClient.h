//
//  RpcClient.h
//  SyncRpc
//
//  Created by Aleksander Tsarenko on 13.07.22.
//

#include "RpcReplicatorCommands.h"

#include <thread>

#include <boost/asio/connect.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>

#include <boost/lambda/bind.hpp>
#include <boost/lambda/lambda.hpp>

#include <cereal/types/string.hpp>
#include <cereal/types/array.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/map.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/archives/portable_binary.hpp>


namespace asio = boost::asio;
using     tcp  = boost::asio::ip::tcp;


class RpcTcpSocket
{
    asio::io_context        m_context;
    tcp::socket             m_socket;

public:
    RpcTcpSocket() : m_socket(m_context)
    {
    }
    
    ~RpcTcpSocket()
    {
        m_socket.close();
    }
    
    void connect( std::string address, std::string port )
    {
        tcp::resolver::query query( address, port );
        tcp::resolver::iterator iter = tcp::resolver( m_context ).resolve(query);

        boost::system::error_code ec = boost::asio::error::would_block;
        boost::asio::async_connect( m_socket, iter, boost::lambda::var(ec) = boost::lambda::_1);
        do m_context.run_one(); while (ec == boost::asio::error::would_block);

        if (ec || !m_socket.is_open())
        {
          throw boost::system::system_error(
              ec ? ec : boost::asio::error::operation_aborted);
        }
    }
    
    void sendCommand( RPC_CMD command )
    {
        __LOG( "sendCommand: " << CMD_STR(command) )

        boost::system::error_code ec = boost::asio::error::would_block;
        asio::write( m_socket, asio::buffer( &command, sizeof(command) ), ec );
        if (ec)
        {
            __LOG( "!write error!: " << ec.what() )
            exit(0);
        }

        uint16_t packetLen = 0;
        boost::asio::write( m_socket, asio::buffer( &packetLen, sizeof(packetLen) ), ec );

        if (ec)
        {
            __LOG( "!write error!: " << ec.what() )
            exit(0);
        }
        __LOG( "sendCommand done: " << CMD_STR(command) )
    }

    void sendCommand( RPC_CMD command, const std::string& parameters )
    {
        __LOG( "sendCommand: " << CMD_STR(command) )

        boost::system::error_code ec = boost::asio::error::would_block;
        asio::write( m_socket, asio::buffer( &command, sizeof(command) ), ec );
        if (ec)
        {
            __LOG( "!write error!: " << ec.what() )
            exit(0);
        }

        uint16_t packetLen = 0;
        boost::asio::write( m_socket, asio::buffer( &packetLen, sizeof(packetLen) ), ec );

        if (ec)
        {
            __LOG( "!write error! (2): " << ec.what() )
            exit(0);
        }

        boost::asio::write( m_socket, asio::buffer( parameters.c_str(), parameters.size() ), ec );

        if (ec)
        {
            __LOG( "!write error! (3): " << ec.what() )
            exit(0);
        }

        __LOG( "sendCommand(2) done: " << CMD_STR(command) )
    }

    RPC_CMD readPacket( asio::streambuf& streambuf )
    {
        __LOG( "readPacket(): started" )

        boost::system::error_code ec;

        RPC_CMD command;
        auto len = asio::read( m_socket, asio::buffer( &command, sizeof(command) ), ec );

        if( ec )
        {
            __LOG( "ec: " << ec.what() )
            exit(0);
        }
        
        if ( len != sizeof(command) )
        {
            __LOG( "bad len: " << len << " "  << sizeof(command) )
            exit(0);
        }
                              
        __LOG( "child: command: " << CMD_STR(command) )
        
        uint16_t packetLen;
        len = asio::read( m_socket, asio::buffer( &packetLen, sizeof(packetLen) ), asio::transfer_exactly( sizeof(packetLen) ), ec );

        if( ec )
        {
            __LOG( "ec(2): " << ec.what() )
            exit(0);
        }
        
        if ( len != sizeof(packetLen) )
        {
            __LOG( "bad len(2): " << len << " "  << sizeof(packetLen) )
            exit(0);
        }

        if ( len > 0 )
        {
            streambuf.prepare( packetLen );

            auto len = asio::read( m_socket, streambuf, asio::transfer_exactly( packetLen ), ec );

            if( ec )
            {
                __LOG( "ec(3): " << ec.what() )
                exit(0);
            }
            
            if ( len != sizeof(packetLen) )
            {
                __LOG( "bad len(3): " << len << " "  << sizeof(packetLen) )
                exit(0);
            }

            streambuf.consume( len );
        }
        
        return command;
    }

    RPC_CMD readCommand()
    {
        RPC_CMD command;
        boost::system::error_code ec;
        auto len = asio::read( m_socket, asio::buffer( &command, sizeof(command) ), asio::transfer_exactly( sizeof(command) ), ec );
        assert( !ec && len == sizeof(command) );
        return command;
    }
};

class RpcClient
{
    RpcTcpSocket    m_upSocket; // from client to server
    RpcTcpSocket    m_dnSocket; // from server to client
    
    std::mutex      m_upMutex;

protected:

    RpcClient()
    {
    }
    
    virtual ~RpcClient() = default;
    
    virtual void handleCommand( RPC_CMD command, cereal::PortableBinaryInputArchive& parameters ) = 0;

    virtual void handleError( std::error_code ) = 0;

    virtual void handleConnectionLost() = 0;
    
public:
    
    void run( std::string address, std::string port )
    {
        std::thread( [this,address,port]
        {
            boost::asio::streambuf inStreambuf;
            
            m_upSocket.connect( address, port );
            m_upSocket.sendCommand( RPC_CMD::UP_CHANNEL_INIT );

            m_dnSocket.connect( address, port );
            m_dnSocket.sendCommand( RPC_CMD::DOWN_CHANNEL_INIT );
            
            std::thread( [this]
            {
                for(;;)
                {
                    sleep(1);
                    std::unique_lock<std::mutex> lock(m_upMutex);
                    m_dnSocket.sendCommand( RPC_CMD::PING );
                }
            }).detach();
            
            for(;;)
            {
                //__LOG( "m_dnSocket.readPacket: " )
                RPC_CMD command = m_dnSocket.readPacket( inStreambuf );
                
                switch (command)
                {
                    case RPC_CMD::dbgCrash:
                    {
                        __LOG( "!!! switch RPC_CMD::dbgCrash" );
                        //*((int*)0) = 42;
                        abort();
                        __LOG( "!!!!!!!!!!!!!!!!!!!!!!!!!" );
                        break;
                    }
                    case RPC_CMD::PING:
                    {
                        m_upSocket.sendCommand( RPC_CMD::dbgCrash );
                        break;
                    }
                    default:
                    {
                        std::istream is( &inStreambuf );
                        cereal::PortableBinaryInputArchive iarchive(is);
                        handleCommand( command, iarchive );
                        break;
                    }
                };
                
                __LOG( "inStreambuf.size: " << inStreambuf.size() )
            }
        }).detach();
    }
    
    void sendAnswer( RPC_CMD func )
    {
        m_dnSocket.sendCommand( func );
    }
    
    void rpcCall( RPC_CMD func )
    {
        std::unique_lock<std::mutex> lock(m_upMutex);
        m_upSocket.sendCommand( func );
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

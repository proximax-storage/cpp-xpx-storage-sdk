//
//  RpcParent.h
//  SyncRpc
//
//  Created by Aleksander Tsarenko on 13.07.22.
//

#pragma once

#include <unistd.h>
#include <atomic>
#include <stdexcept>

#include <cereal/types/string.hpp>
#include <cereal/types/array.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/map.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/archives/portable_binary.hpp>

#include "RpcTcpServer.h"

class RpcServer : public RpcTcpServer
{
    pid_t m_childPid = 0;
    
public:
    
    void startService()
    {
    }
    
    void restartService()
    {
        
    }
    
    void stop()
    {
        
    }
    
    void rpcCall( RPC_CMD func )
    {
        RpcTcpServer::rpcCall( func, "" );
    }

    void rpcCallArchStr( RPC_CMD func, const std::string& str )
    {
        RpcTcpServer::rpcCall( func, str );
    }

    template<class K>
    std::array<uint8_t,32> rpcDbgGetRootHash( K& key )
    {
        std::ostringstream os( std::ios::binary );
        cereal::PortableBinaryOutputArchive archive( os );
        archive( key );

        return RpcTcpServer::rpcDbgGetRootHash( os.str() );
    }

    template<class T>
    void rpcCall( RPC_CMD func, const T& p )
    {
        std::ostringstream os( std::ios::binary );
        cereal::PortableBinaryOutputArchive archive( os );
        archive( p );

        RpcTcpServer::rpcCall( func, os.str() );
    }

    template<class T,class T2>
    void rpcCall( RPC_CMD func, const T& p, const T2& p2 )
    {
        std::ostringstream os( std::ios::binary );
        cereal::PortableBinaryOutputArchive archive( os );
        archive( p );
        archive( p2 );

        RpcTcpServer::rpcCall( func, os.str() );
    }

    template<class T,class T2,class T3>
    void rpcCall( RPC_CMD func, const T& p, const T2& p2, const T3& p3 )
    {
        std::ostringstream os( std::ios::binary );
        cereal::PortableBinaryOutputArchive archive( os );
        archive( p );
        archive( p2 );
        archive( p3 );

        RpcTcpServer::rpcCall( func, os.str() );
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

        RpcTcpServer::rpcCall( func, os.str() );
    }


protected:
public:
    RpcServer( std::string address, int port ) : RpcTcpServer()
    {
        startTcpServer( address, port );
    }

    virtual void handleCommand( RPC_CMD command, cereal::PortableBinaryInputArchive* parameters ) {}

    virtual void handleError( std::error_code ) {}

    virtual void handleConnectionLost() {}

};

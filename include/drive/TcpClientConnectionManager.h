/*
*** Copyright 2024 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include <list>
#include <streambuf>

#include <boost/asio.hpp>

#include <cereal/types/vector.hpp>
#include <cereal/types/array.hpp>
#include <cereal/types/map.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/types/string.hpp>
#include <cereal/archives/binary.hpp>

#include "TcpClient.h"
#include "TcpConnectionProtocol.h"

namespace sirius { namespace drive {

class ITcpResponseHandler
{
public:
    virtual ~ITcpResponseHandler() = default;
    
    virtual void onResponseReceived( bool success, uint8_t* data, size_t dataSize ) = 0;
};


class TcpClientConnectionManager: public std::enable_shared_from_this<TcpClientConnectionManager>, public IClientConnectionManager
{
    ITcpResponseHandler&     m_responseHandler;
    boost::asio::io_context& m_ioContext;
    
    std::list< std::shared_ptr<TcpClient> > m_connections;
    
public:
    TcpClientConnectionManager( ITcpResponseHandler& responseHandler, boost::asio::io_context& ioContext )
        : m_responseHandler(responseHandler), m_ioContext(ioContext)
    {}
    
    template<class PacketT>
    void sendRequestTo( boost::asio::ip::tcp::endpoint replicatorEndpoint,
                       TcpRequestId requestId,
                       const PacketT& request )
    {
        m_connections.push_back( std::make_shared<TcpClient>( *this, m_ioContext ) );
        std::ostringstream os( std::ios::binary );
        cereal::BinaryOutputArchive archive( os );
        archive( uint16_t{} );
        archive( uint16_t(requestId) );
        archive( request );
        
        m_connections.back()->sendRequestTo( replicatorEndpoint, os.str() );
    }
    
    
    struct Buffer : public std::streambuf
    {
        Buffer( char* data, size_t dataSize ) { setg( data, data, data + dataSize ); }
    };
    
    void onResponseReceived( bool success, uint8_t* data, size_t dataSize, std::shared_ptr<TcpClient> connection ) override
    {
        m_responseHandler.onResponseReceived( success, data, dataSize );
        
        m_connections.remove_if( [&connection](auto& instance) { return instance.get() == connection.get(); } );
    }
};

}}

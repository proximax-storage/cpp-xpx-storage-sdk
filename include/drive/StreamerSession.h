/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "drive/Session.h"
#include "drive/Streaming.h"
#include "ClientSession.h"
#include "drive/log.h"
#include "drive/Utils.h"
#include "crypto/Signer.h"

#include <sirius_drive/session_delegate.h>

#include <iostream>
#include <fstream>

namespace sirius::drive {

class StreamerSession : public ClientSession, public DhtMessageHandler
{
    std::optional<Hash256>  m_streamId;
    Key                     m_driveKey;
    
    using udp_endpoint_list = std::set<boost::asio::ip::udp::endpoint>;
    udp_endpoint_list       m_endPointList;
    
    fs::path                m_chunkFolder;
    fs::path                m_torrentFolder;
    
    using ChunkInfoMap = std::map<uint32_t,ChunkInfo>;
    ChunkInfoMap            m_chunkInfoMap;
    uint32_t                m_firstChunkIndex = 0;
    uint32_t                m_lastChunkIndex  = 0;
    uint64_t                m_totalChunkBytes;

    const std::string       m_dbgOurPeerName;
    
    std::mutex              m_chunkMutex;
    
public:
    StreamerSession( const crypto::KeyPair& keyPair, const char* dbgOurPeerName )
        :
            ClientSession( keyPair, dbgOurPeerName ),
            m_dbgOurPeerName(dbgOurPeerName)
    {}

    ~StreamerSession()
    {
    }
    
    void initStream( const Hash256& streamId,
                        const Key& driveKey,
                        const fs::path& workFolder,
                        const endpoint_list& endPointList )
    {
        _ASSERT( ! m_streamId )
        _ASSERT( endPointList.size() > 0 )

        m_streamId = streamId;
        m_driveKey = driveKey;

        for( const auto& endpoint : endPointList )
        {
            m_endPointList.emplace( endpoint.address(), endpoint.port() );
        }
        
        m_totalChunkBytes = 0;

        fs::remove_all( workFolder );
        m_chunkFolder = workFolder / "chunks";
        m_torrentFolder = workFolder / "torrents";

        fs::create_directories( m_chunkFolder );
        fs::create_directories( m_torrentFolder );
    }
    
    void addChunkToStream( const std::vector<uint8_t>& chunk, uint32_t durationMs, bool dbgEmulateLostDhtMessage = false )
    {
        if ( ! m_streamId )
        {
            _LOG_ERR( "m_streamId is not set" );
            return;
        }
        
        std::lock_guard<std::mutex> lock( m_chunkMutex );
        
        fs::path tmp = m_chunkFolder / "newChunk";
        
        {
            std::ofstream fileStream( tmp, std::ios::binary );
            fileStream.write( (char*) chunk.data(), chunk.size() );
        }

        //InfoHash chunkHash = calculateInfoHashAndCreateTorrentFile( tmp, m_keyPair.publicKey(), m_torrentFolder, "" );
        InfoHash chunkHash = createTorrentFile( tmp, m_keyPair.publicKey(), m_chunkFolder, {} );
        fs::path chunkFilename = m_chunkFolder / toString( chunkHash );

        _LOG( "*** chunkFilename: " << chunkFilename );

        // add chunk to libtorrent session
        if ( fs::exists( chunkFilename ) )
        {
            _LOG( "*** Chunk already exists: " << chunkFilename );
            fs::remove( tmp );
        }
        else
        {
            fs::rename( tmp, chunkFilename );

            fs::path torrentFilename = m_torrentFolder / toString( chunkHash );
            InfoHash chunkHash2 = createTorrentFile( chunkFilename, m_keyPair.publicKey(), m_chunkFolder, torrentFilename );
            _ASSERT( chunkHash2 == chunkHash )

            lt_handle torrentHandle = m_session->addTorrentFileToSession( torrentFilename,
                                                                          m_chunkFolder,
                                                                          lt::SiriusFlags::client_has_modify_data,
                                                                          &m_keyPair.publicKey().array(),
                                                                          nullptr,
                                                                          nullptr, //&m_streamId->array(),
                                                                          {},
                                                                          nullptr );
            m_totalChunkBytes += chunk.size();
        }

        auto [it,ok] = m_chunkInfoMap.emplace( m_lastChunkIndex,
                               ChunkInfo{ m_streamId->array(), m_lastChunkIndex, chunkHash.array(), durationMs, chunk.size(), {} } );
        m_lastChunkIndex++;
        _ASSERT( ok )

        auto& chunkInfo = it->second;
        chunkInfo.Sign( m_keyPair );
        std::ostringstream os( std::ios::binary );
        cereal::PortableBinaryOutputArchive archive( os );
        auto driveKey = m_driveKey.array();
        archive( driveKey );
        archive( chunkInfo );

        if ( ! dbgEmulateLostDhtMessage )
        {
            for( auto& endpoint : m_endPointList )
            {
                m_session->sendMessage( "chunk-info", endpoint, os.str() );
            }
        }
    }
    
    void sendSingleChunkInfo( uint32_t index, const boost::asio::ip::udp::endpoint& endpoint )
    {
        std::lock_guard<std::mutex> lock( m_chunkMutex );

        if ( auto it = m_chunkInfoMap.find( index ); it != m_chunkInfoMap.end() )
        {
            const ChunkInfo& chunkInfo = it->second;
            std::ostringstream os( std::ios::binary );
            cereal::PortableBinaryOutputArchive archive( os );
            auto driveKey = m_driveKey.array();
            archive( driveKey );
            archive( chunkInfo );

            m_session->sendMessage( "chunk-info", endpoint, os.str() );
        }
    }
    
    virtual bool on_dht_request( lt::string_view                         query,
                                 boost::asio::ip::udp::endpoint const&   source,
                                 lt::bdecode_node const&                 message,
                                 lt::entry&                              response ) override
    {
        if ( query == "get-chunk-info" )
        {
            if ( m_endPointList.find( source) != m_endPointList.end() )
            {
                try
                {
                    auto str = message.dict_find_string_value("x");
                    std::string packet( (char*)str.data(), (char*)str.data()+str.size() );

                    std::istringstream is( packet, std::ios::binary );
                    cereal::PortableBinaryInputArchive iarchive(is);
                    uint32_t chunkIndex;
                    iarchive( chunkIndex );

                    sendSingleChunkInfo( chunkIndex, source );
                }
                catch(...)
                {
                    _LOG_ERR( "invalid message 'get-chunk-info'" );
                }
            }
        }
        
        return false;
    }

};

inline std::shared_ptr<StreamerSession> createStreamerSession( const crypto::KeyPair&        keyPair,
                                                               const std::string&            address,
                                                               const LibTorrentErrorHandler& errorHandler,
                                                               const endpoint_list&          bootstraps,
                                                               bool                          useTcpSocket, // instead of uTP
                                                               const char*                   dbgClientName = "" )
{
    std::shared_ptr<StreamerSession> session = std::make_shared<StreamerSession>( keyPair, dbgClientName );
    session->m_session = createDefaultSession( address, errorHandler, session, bootstraps, session );
    session->session()->lt_session().m_dbgOurPeerName = dbgClientName;
    return session;
}

}

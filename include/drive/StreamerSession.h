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

class StreamerSession : public ClientSession
{
    std::optional<Hash256>  m_streamId;
    Key                     m_driveKey;
    
    endpoint_list           m_endPointList;
    
    fs::path                m_chunkFolder;
    fs::path                m_torrentFolder;
    
    std::deque<Hash256>     m_chunkHashes;
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
    
    void initStreaming( const Hash256& streamId,
                        const Key& driveKey,
                        const fs::path& workFolder,
                        const endpoint_list& endPointList )
    {
        _ASSERT( ! m_streamId )
        _ASSERT( endPointList.size() > 0 )

        m_streamId = streamId;
        m_driveKey = driveKey;
        m_endPointList = endPointList;
        
        m_totalChunkBytes = 0;

        fs::remove_all( workFolder );
        m_chunkFolder = workFolder / "chunks";
        m_torrentFolder = workFolder / "torrents";

        fs::create_directories( m_chunkFolder );
        fs::create_directories( m_torrentFolder );
    }
    
    void addChunk( const std::vector<uint8_t>& chunk, uint32_t durationMs )
    {
        if ( ! m_streamId )
        {
            _LOG_ERR( "m_streamId is not set" );
            return;
        }
        
        std::lock_guard<std::mutex> lock( m_chunkMutex );
        
        //??? BG_THREAD?
        
        fs::path tmp = m_chunkFolder / "newChunk";
        
        std::ofstream fileStream( tmp, std::ios::binary );
        fileStream.write( (char*) chunk.data(), chunk.size() );

        //InfoHash chunkHash = calculateInfoHashAndCreateTorrentFile( tmp, m_keyPair.publicKey(), m_torrentFolder, "" );
        InfoHash chunkHash = createTorrentFile( tmp, m_keyPair.publicKey(), m_chunkFolder, {} );
        fs::path chunkFilename = m_chunkFolder / toString( chunkHash );

        // add chunk to libtorrent session
        if ( fs::exists( chunkFilename ) )
        {
            _LOG( "Chunk already exists" );
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
                                                                          &chunkHash.array(),
                                                                          nullptr,
                                                                          &m_keyPair.publicKey().array(),
                                                                          {},
                                                                          &m_totalChunkBytes );
        }
        
        m_chunkHashes.push_back( chunkHash );
        uint32_t chunkIndex = (uint32_t) m_chunkHashes.size()-1;
        
        ChunkInfo chunkInfo { m_streamId->array(), chunkIndex, chunkHash.array(), durationMs, chunk.size(), {} };
        chunkInfo.Sign( m_keyPair );
        std::ostringstream os( std::ios::binary );
        cereal::PortableBinaryOutputArchive archive( os );
        auto driveKey = m_driveKey.array();
        archive( driveKey );
        archive( chunkInfo );

        for( auto& endpoint : m_endPointList )
        {
            boost::asio::ip::udp::endpoint udp( endpoint.address(), endpoint.port() );
            m_session->sendMessage( "chunk-info", udp, os.str() );
        }
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
    session->m_session = createDefaultSession( address, errorHandler, session, bootstraps, useTcpSocket );
    session->session()->lt_session().m_dbgOurPeerName = dbgClientName;
    return session;
}

}

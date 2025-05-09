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

#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/extensions.hpp"
#include <sirius_drive/session_delegate.h>

#include <iostream>
#include <fstream>

namespace sirius::drive {

struct FinishStreamInfo
{
    InfoHash infoHash;
    uint64_t streamSizeBytes;
};

class StreamerSession : public ClientSession, public DhtMessageHandler, public lt::plugin
{
protected:
    std::optional<Hash256>  m_streamId;
    Key                     m_driveKey;
    
    using udp_endpoint_list = std::set<boost::asio::ip::udp::endpoint>;
    udp_endpoint_list       m_endPointList;
    
    fs::path                m_m3u8Playlist;
    fs::path                m_mediaFolder;
    fs::path                m_chunkFolder;
    fs::path                m_torrentFolder;
    
    using ChunkInfoMap = std::map<uint32_t,ChunkInfo>;
    ChunkInfoMap            m_chunkInfoMap;
    uint32_t                m_firstChunkIndex = 0;
    uint32_t                m_lastChunkIndex  = 0;
    uint64_t                m_totalChunkBytes;

    const std::string       m_dbgOurPeerName;
    
    std::mutex              m_chunkMutex;
    
    boost::asio::io_context                                                     m_bgContext;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type>    m_bgWork;
    std::thread                                                                 m_bgThread;
    boost::asio::high_resolution_timer                                          m_tickTimer;
    
    int m_startSequenceNumber = -1;
    int m_lastSequenceNumber = -1;


public:
    StreamerSession( const crypto::KeyPair& keyPair, const char* dbgOurPeerName )
        :
            ClientSession( keyPair, dbgOurPeerName ),
            m_dbgOurPeerName(dbgOurPeerName),
            m_bgContext(),
            m_bgWork(boost::asio::make_work_guard(m_bgContext)),
            m_bgThread( std::thread( [this] { m_bgContext.run(); } )),
            m_tickTimer( m_bgContext )
    {
        planNextTick();
    }
    
    void planNextTick()
    {
        m_tickTimer.expires_after( std::chrono::milliseconds( 1000 ) );
        m_tickTimer.async_wait( [this] ( boost::system::error_code const& e )
        {
            if ( ! e )
            {
                onTick();
                planNextTick();
            }
        });
    }

    ~StreamerSession()
    {
        m_tickTimer.cancel();
        m_bgContext.stop();
        if ( m_bgThread.joinable() )
        {
            m_bgThread.join();
        }
    }
    
    std::optional<boost::asio::ip::udp::endpoint> getEndpoint( const Key& key )
    {
        return m_endpointsManager.getEndpoint( key );
    }
    
    void initStream( const Hash256&          streamId,
                     const Key&              driveKey,
                     const fs::path&         m3u8Playlist,
                     const fs::path&         chunksFolder,
                     const fs::path&         torrentsFolder,
                     const endpoint_list&    endPointList )
    {
        SIRIUS_ASSERT( ! m_streamId )
        SIRIUS_ASSERT( endPointList.size() > 0 )

        m_streamId = streamId;
        m_driveKey = driveKey;
        m_chunkInfoMap.clear();

        for( const auto& endpoint : endPointList )
        {
            m_endPointList.emplace( endpoint.address(), endpoint.port() );
        }
        
        m_totalChunkBytes = 0;

        m_m3u8Playlist  = m3u8Playlist;
        m_mediaFolder   = fs::path(m3u8Playlist).parent_path();
        m_chunkFolder   = chunksFolder;
        m_torrentFolder = torrentsFolder;

        fs::create_directories( m_chunkFolder );
        fs::create_directories( m_torrentFolder );
    }
    
    void cancelStream()
    {
        m_streamId.reset();
    }
    
    FinishStreamInfo finishStream( uint64_t startTimeSecods = 0, uint64_t endTimeSecods = std::numeric_limits<uint64_t>::max() )
    {
        if ( ! m_streamId )
        {
            throw std::runtime_error("no active stream");
        }
        
        std::vector<FinishStreamChunkInfo> finishStreamInfo;
        
        uint64_t timeMks = 0;
        uint64_t streamSize = 0;
        for( uint32_t i=0; (i < m_chunkInfoMap.size()); i++ )
        {
            auto chunkInfoIt = m_chunkInfoMap.find(i);
            SIRIUS_ASSERT( chunkInfoIt != m_chunkInfoMap.end() )
            
            if ( (timeMks + chunkInfoIt->second.m_durationMks < startTimeSecods*1000000)
                || (timeMks > endTimeSecods*1000000) )
            {
                finishStreamInfo.emplace_back( FinishStreamChunkInfo{ i, chunkInfoIt->second.m_chunkInfoHash,
                                                                chunkInfoIt->second.m_durationMks,
                                                                chunkInfoIt->second.m_sizeBytes,
                                                                false } );
                timeMks += chunkInfoIt->second.m_durationMks;
                continue;
            }
            
            finishStreamInfo.emplace_back( FinishStreamChunkInfo{ i, chunkInfoIt->second.m_chunkInfoHash,
                                                            chunkInfoIt->second.m_durationMks,
                                                            chunkInfoIt->second.m_sizeBytes,
                                                            true } );
            timeMks += chunkInfoIt->second.m_durationMks;
            
            std::error_code ec;
            auto fileSize = fs::file_size( m_chunkFolder / toString(chunkInfoIt->second.m_chunkInfoHash), ec );
            _LOG( "fileSize: " << fileSize );
            if ( ! ec )
            {
                streamSize += fileSize;
            }
            auto torrentFileSize = fs::file_size( m_torrentFolder / toString(chunkInfoIt->second.m_chunkInfoHash), ec );
            _LOG( "torrentFileSize: " << torrentFileSize );
            if ( ! ec )
            {
                streamSize += torrentFileSize;
            }
            _LOG( "streamSize: " << streamSize );
        }
        
//        std::ostringstream os( std::ios::binary );
//        cereal::PortableBinaryOutputArchive archive( os );
//        archive( finishStreamInfo );

        fs::path finishStreamFilename = m_chunkFolder / "finishStreamInfo";
        {
            std::ofstream finishStreamFile( finishStreamFilename, std::ios::binary );
            cereal::PortableBinaryOutputArchive archive( finishStreamFile );
            archive( finishStreamInfo );
        }

        fs::path torrentFilename = m_torrentFolder / "finishStreamInfo";
        InfoHash infoHash = createTorrentFile( finishStreamFilename, m_keyPair.publicKey(), m_chunkFolder, torrentFilename );

        lt_handle torrentHandle = m_session->addTorrentFileToSession( torrentFilename.make_preferred(),
                                                                      m_chunkFolder.make_preferred(),
                                                                      lt::SiriusFlags::client_has_modify_data,
                                                                      &m_keyPair.publicKey().array(),
                                                                      nullptr,
                                                                      &m_streamId->array(),
                                                                      {},
                                                                      nullptr );

//        {
//            FinishStreamMsg finishStream{ m_streamId->array(), infoHash.array(), {} };
//            finishStream.Sign( m_keyPair );
//
//            std::ostringstream os( std::ios::binary );
//            cereal::PortableBinaryOutputArchive archive( os );
//            auto driveKey = m_driveKey.array();
//            archive( driveKey );
//            archive( finishStream );
//
//            for( auto& endpoint : m_endPointList )
//            {
//                m_session->sendMessage( "finish-stream", endpoint, os.str() );
//            }
//        }
        
        m_streamId.reset();
        return { infoHash, streamSize };
    }
    
    void addChunkToStream( const std::vector<uint8_t>& chunk, uint32_t durationMs, InfoHash* dbgInfoHash = nullptr )
    {
        if ( ! m_streamId )
        {
            _LOG_ERR( "m_streamId is not set" );
            return;
        }
        
        fs::path tmp = m_mediaFolder / ("newChunk" + std::to_string(m_lastChunkIndex));
        
        {
            std::ofstream fileStream( tmp, std::ios::binary );
            fileStream.write( (char*) chunk.data(), chunk.size() );
        }

        addMediaToStream( tmp, durationMs, dbgInfoHash );
    }

    void addMediaToStream( const fs::path& tmp, uint32_t durationMs, InfoHash* dbgInfoHash = nullptr )
    {
        _LOG( "addMediaToStream: " << tmp );
        
        std::lock_guard<std::mutex> lock( m_chunkMutex );
        
        uint64_t chunkSize = fs::file_size( tmp );
        
        InfoHash chunkHash = createTorrentFile( fs::path(tmp).make_preferred(), m_keyPair.publicKey(), m_mediaFolder.make_preferred(), {} );
        fs::path chunkFilename = (m_chunkFolder / toString( chunkHash )).make_preferred();

        if ( dbgInfoHash != nullptr )
        {
            *dbgInfoHash = chunkHash;
        }

        // add chunk to libtorrent session
        if ( fs::exists( chunkFilename ) )
        {
//            _LOG_WARN( "*** Chunk already exists: " << chunkFilename );
            _LOG( "*** Chunk already exists: " << chunkFilename );
        }
        else
        {
            std::error_code ec;
            
            _LOG( "Copy chunk: " << tmp << " to: " << chunkFilename );
            //TODO:
            //fs::rename( tmp, chunkFilename, ec );
            fs::copy( tmp, chunkFilename, ec );

            if ( ec )
            {
                _LOG_WARN( "*** Cannot copy: " << ec.message() );
                return;
            }

            fs::path torrentFilename = m_torrentFolder / toString( chunkHash );
            InfoHash chunkHash2 = createTorrentFile( chunkFilename.make_preferred(), m_keyPair.publicKey(), m_chunkFolder.make_preferred(), torrentFilename.make_preferred() );
            SIRIUS_ASSERT( chunkHash2 == chunkHash )

            lt_handle torrentHandle = m_session->addTorrentFileToSession( torrentFilename.make_preferred(),
                                                                          m_chunkFolder.make_preferred(),
                                                                          lt::SiriusFlags::client_has_modify_data,
                                                                          &m_keyPair.publicKey().array(),
                                                                          nullptr,
                                                                          &m_streamId->array(),
                                                                          {},
                                                                          nullptr );
            m_totalChunkBytes += chunkSize;
        }

        auto [it,ok] = m_chunkInfoMap.emplace( m_lastChunkIndex,
                               ChunkInfo{ m_streamId->array(), m_lastChunkIndex, chunkHash.array(), durationMs, chunkSize, {} } );
        m_lastChunkIndex++;
        SIRIUS_ASSERT( ok )

        auto& chunkInfo = it->second;
        chunkInfo.Sign( m_keyPair );
        std::ostringstream os( std::ios::binary );
        cereal::PortableBinaryOutputArchive archive( os );
        auto driveKey = m_driveKey.array();
        archive( driveKey );
        archive( chunkInfo );

        if ( dbgInfoHash == nullptr )
        {
            for( auto& endpoint : m_endPointList )
            {
                m_session->sendMessage( "chunk-info", endpoint, os.str() );
            }
        }
    }
    
    void sendSingleChunkInfo( uint32_t index, const boost::asio::ip::udp::endpoint& endpoint )
    {
        _LOG( "sendSingleChunkInfo: " << index )
        
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

    void onTick()
    {
        if ( ! m_streamId )
        {
            return;
        }
        
        // Read & Parse playlist/manifest
        parseM3u8Playlist();
    }
    
    std::string parseM3u8Playlist()
    {
        // copy file (it could be chaged)
        std::ifstream fin( m_m3u8Playlist );
        std::stringstream fPlaylist;
        fPlaylist << fin.rdbuf();
        
        std::string line;
        
        if ( ! std::getline( fPlaylist, line ) || memcmp( line.c_str(), "#EXTM3U", 7 ) != 0 )
        {
            return std::string("1-st line of playlist must be '#EXTM3U'");
        }
        
        int sequenceNumber = -1;
        int mediaIndex = 0;
        
        for(;;)
        {
            if ( ! std::getline( fPlaylist, line ) )
            {
                break;
            }

            if ( memcmp( line.c_str(), "#EXT-X-VERSION:", 15 ) == 0 )
            {
                int version;
                try
                {
                    version = std::stoi( line.substr(15) );
                    //_LOG( "version: " << version )
                }
                catch(...)
                {
                    return std::string("invalid playlist format: ") + line;
                }
                
                if ( version != 3 && version != 4 )
                {
                    return std::string("invalid version number: ") + line.substr(15);
                }
                continue;
            }

            if ( memcmp( line.c_str(), "#EXT-X-TARGETDURATION:", 16+6 ) == 0 )
            {
                continue;
            }

            if ( memcmp( line.c_str(), "#EXT-X-MEDIA-SEQUENCE:", 16+6 ) == 0 )
            {
                try
                {
                    sequenceNumber = std::stoi( line.substr(16+6) );
                    //_LOG( "sequenceNumber: " << sequenceNumber )
                }
                catch(...)
                {
                    return std::string("cannot read sequence number: ") + line;
                }
                
                if ( m_startSequenceNumber == -1 || sequenceNumber < m_lastSequenceNumber )
                {
                    m_startSequenceNumber = sequenceNumber;
                }
            }

            if ( memcmp( line.c_str(), "#EXTINF:", 8 ) == 0 )
            {
                float duration;
                try
                {
                    duration = std::stof( line.substr(8) );
                    //_LOG( "duration: " << duration )
                }
                catch(...)
                {
                    return std::string("cannot read duration attribute: ") + line;
                }
                
                if ( ! std::getline( fPlaylist, line ) )
                {
                    break;
                }

                if ( m_startSequenceNumber < 0 )
                {
                    m_startSequenceNumber = sequenceNumber;
                }
                
                uint32_t chunkIndex = sequenceNumber - m_startSequenceNumber + mediaIndex;
                
                if ( m_chunkInfoMap.find( chunkIndex ) == m_chunkInfoMap.end() )
                {
                    addMediaToStream( m_mediaFolder / line, duration*1000000  );
                }
                mediaIndex++;
                for( auto& [key,value] : m_chunkInfoMap )
                {
                    _LOG( "key: " << key << " chunkIndex: " << chunkIndex << "   " << (key==chunkIndex) );
                }
                continue;
            }
        }

        return "";
    }
    
    virtual void dbgAddReplicatorList( const std::vector<ReplicatorInfo>& replicators )
    {
        for( auto& replicatorInfo : replicators )
        {
            m_endpointsManager.onEndpointDiscovered( replicatorInfo.m_publicKey, replicatorInfo.m_endpoint );
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
    session->m_session = createDefaultSession( address, errorHandler, session, { ReplicatorInfo{bootstraps[0],{}}}, session );
    session->m_session->lt_session().add_extension( std::dynamic_pointer_cast<lt::plugin>( session ) );
    session->session()->lt_session().m_dbgOurPeerName = dbgClientName;
    
    boost::asio::post(session->session()->lt_session().get_context(), [session] {
        session->m_endpointsManager.start(session->session());
        session->setEndpointHandler();
    });
    session->addDownloadChannel(Hash256{});

    return session;
}

}

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

struct WatchOptions
{
    uint32_t    m_chunkRequestTimeIntervalMs = 100;
};

class ViewerSession : public ClientSession, public DhtMessageHandler, public lt::plugin
{
    struct ViewerChunkInfo : public ChunkInfo
    {
        uint32_t   m_offsetMs;
        
        ViewerChunkInfo( const ChunkInfo& info, uint32_t  offsetMs ) : ChunkInfo(info), m_offsetMs(offsetMs) {}
    };

    std::optional<Hash256>  m_streamId;     // tx hash
    Key                     m_streamerKey;  // streamer public key
    Key                     m_driveKey;
    WatchOptions            m_watchOptions;
    
    using udp_endpoint_list = std::set<boost::asio::ip::udp::endpoint>;
    udp_endpoint_list       m_replicatorEndpointList;
    
    fs::path                m_chunkFolder;
    fs::path                m_torrentFolder;
    
    using ChunkInfoList = std::deque<ViewerChunkInfo>;
    ChunkInfoList           m_chunkInfoList;

    
    std::optional<lt_handle>    m_downloadingLtHandle;
    uint32_t                    m_tobeDownloadedChunkIndex = 0;

    const std::string       m_dbgOurPeerName;
    
    std::mutex              m_chunkMutex;
    
public:
    ViewerSession( const crypto::KeyPair& keyPair, const char* dbgOurPeerName )
        :
            ClientSession( keyPair, dbgOurPeerName ),
            m_dbgOurPeerName(dbgOurPeerName)
    {
    }

    ~ViewerSession()
    {
    }
    
    feature_flags_t implemented_features() override
    {
        return plugin::tick_feature;
    }

    void on_tick() override
    {
        if ( ! m_streamId )
        {
            return;
        }
        
        if ( m_chunkInfoList.empty() )
        {
            requestChunkInfo(0);
            return;
        }

        requestChunkInfo( m_chunkInfoList.size() );
    }

    void endWatching()
    {
        m_streamId.reset();
        //TODO
    }

    void requestChunkInfo( uint32_t chunkIndex )
    {
        _ASSERT( m_streamId )
        
        std::ostringstream os( std::ios::binary );
        cereal::PortableBinaryOutputArchive archive( os );
        
        archive( m_driveKey.array() );
        archive( chunkIndex );

        for( auto& endpoint : m_replicatorEndpointList )
        {
            m_session->sendMessage( "get-chunks-info", endpoint, os.str() );
        }
    }
    
    void startWatching( const Hash256&          streamId,
                        const Key&              streamerKey,
                        const Key&              driveKey,
                        const fs::path&         workFolder,
                        const endpoint_list&    replicatorEndpointList,
                        const WatchOptions      watchOptions = {} )
    {
        _ASSERT( ! m_streamId )
        _ASSERT( replicatorEndpointList.size() > 0 )

        m_streamId = streamId;
        m_streamerKey = streamerKey;
        m_driveKey = driveKey;
        m_watchOptions = watchOptions;

        for( const auto& endpoint : replicatorEndpointList )
        {
            m_replicatorEndpointList.emplace( endpoint.address(), endpoint.port() );
        }
        
        fs::remove_all( workFolder );
        m_chunkFolder = workFolder / "chunks";
        m_torrentFolder = workFolder / "torrents";

        fs::create_directories( m_chunkFolder );
        fs::create_directories( m_torrentFolder );
        
        updatePlaylist( 0 );
    }
    
    void updatePlaylist( uint32_t lastChunkIndex )
    {
        const uint32_t maxChunkNumber = 5;
        uint32_t chunkNumber;
        uint32_t sequenceNumber;

        if ( m_chunkInfoList.size() > maxChunkNumber )
        {
            chunkNumber = maxChunkNumber;
            sequenceNumber = uint32_t(m_chunkInfoList.size()) - maxChunkNumber;
        }
        else
        {
            chunkNumber = uint32_t(m_chunkInfoList.size());
            sequenceNumber = 0;
        }
        
        
        std::stringstream playlist;
        playlist << "#EXTM3U" << std::endl;
        playlist << "#EXT-X-VERSION:3" << std::endl;
        playlist << "#EXT-X-MEDIA-SEQUENCE:" << sequenceNumber << std::endl;
        
        for( uint32_t i = chunkNumber; i < uint32_t(m_chunkInfoList.size()); i++ )
        {
            playlist << "#EXTINF:" << m_chunkInfoList[i].m_durationMks/1000000 << "." << m_chunkInfoList[i].m_durationMks%1000000 << std::endl;
            playlist << Key(m_chunkInfoList[i].m_chunkInfoHash) << std::endl;
        }
        
        //_LOG( ">>>>>>>>123123123\n" << playlist.str() )
        
        auto playlistTxt = playlist.str();
        std::ofstream fileStream( fs::path(m_chunkFolder) / "stream.m3u8", std::ios::binary );
        fileStream.write( playlistTxt.c_str(), playlistTxt.size() );
    }
    
    void handleDhtResponse( lt::bdecode_node response ) override
    {
        auto rDict = response.dict_find_dict("r");
        auto query = rDict.dict_find_string_value("q");

        try
        {
            if ( query == "get-chunks-info" )
            {
                lt::string_view ret = rDict.dict_find_string_value("ret");
                std::string result( ret.begin(), ret.end() );

                std::istringstream is( result, std::ios::binary );
                cereal::PortableBinaryInputArchive iarchive(is);

                uint32_t  chunkIndex;
                iarchive( chunkIndex );
                uint32_t  chunkNumber;
                iarchive( chunkNumber );
                _LOG( "chunkIndex:" << chunkIndex << " chunkNumber:" << chunkNumber )
                
                for( uint32_t i=0; i<chunkNumber; i++ )
                {
                    ChunkInfo info;
                    iarchive( info );
                 
                    if ( ! addChunkInfo( info ) )
                    {
                        return;
                    }
                }
                
                tryLoadNextChunk();
            }
        }
        catch( std::exception& ex )
        {
            _LOG_WARN( "exception: " << ex.what() )
        }
        catch(...)
        {
            _LOG_WARN("exception!")
        }
    }
    
    bool addChunkInfo( const ChunkInfo& info )
    {
        uint32_t newIndex = m_chunkInfoList.size();
        if ( newIndex != info.m_chunkIndex )
        {
            return (newIndex > info.m_chunkIndex);
        }
        
        if ( ! info.Verify(m_streamerKey) )
        {
            _LOG_WARN( "bad sign" );
            return false;
        }
        
        uint32_t offsetMs = m_chunkInfoList.empty() ? 0 : m_chunkInfoList.back().m_offsetMs + m_chunkInfoList.back().m_durationMks;
        m_chunkInfoList.emplace_back( info, offsetMs );
        
        return true;
    }

    void tryLoadNextChunk()
    {
        if ( ! m_downloadChannelId )
        {
            _LOG_ERR( "m_downloadChannelId was not set");
        }
        if ( m_downloadingLtHandle || m_chunkInfoList.size() <= m_tobeDownloadedChunkIndex )
        {
            return;
        }
        
        _LOG( "m_tobeDownloadedChunkIndex: " << m_tobeDownloadedChunkIndex )
        const auto& chunkInfo = m_chunkInfoList[m_tobeDownloadedChunkIndex];
        _ASSERT( m_tobeDownloadedChunkIndex == chunkInfo.m_chunkIndex )
        m_tobeDownloadedChunkIndex++;

        m_downloadingLtHandle = m_session->download(
                           DownloadContext(
                                   DownloadContext::chunk_from_drive,

                                   [this]( download_status::code code,
                                           const InfoHash& infoHash,
                                           const std::filesystem::path saveAs,
                                           size_t downloadedSize,
                                           size_t /*fileSize*/,
                                           const std::string& errorText )
                                   {
                                       if ( code == download_status::download_complete )
                                       {
                                           updatePlaylist( m_tobeDownloadedChunkIndex-1 );
                                           m_downloadingLtHandle.reset();
                                           tryLoadNextChunk();
                                       }
                                       else if ( code == download_status::dn_failed )
                                       {
                                       }
                                   },

                                   InfoHash(chunkInfo.m_chunkInfoHash),
                                   *m_streamId,
                                   0,
                                   true, {}
                           ),

                           m_chunkFolder,
                           m_torrentFolder / (toString(InfoHash(chunkInfo.m_chunkInfoHash))),
                           {}, //getUploaders(),
                           &m_driveKey.array(),
                           &(*m_downloadChannelId),
                           &m_streamId->array()
                        );
    }
    
    virtual bool on_dht_request( lt::string_view                         query,
                                 boost::asio::ip::udp::endpoint const&   source,
                                 lt::bdecode_node const&                 message,
                                 lt::entry&                              response ) override
    {
        // unused
        return false;
    }

};

inline std::shared_ptr<ViewerSession> createViewerSession( const crypto::KeyPair&        keyPair,
                                                           const std::string&            address,
                                                           const LibTorrentErrorHandler& errorHandler,
                                                           const endpoint_list&          bootstraps,
                                                           bool                          useTcpSocket, // instead of uTP
                                                           const char*                   dbgClientName = "" )
{
    std::shared_ptr<ViewerSession> session = std::make_shared<ViewerSession>( keyPair, dbgClientName );
    session->m_session = createDefaultSession( address, errorHandler, session, bootstraps, session );
    session->m_session->lt_session().add_extension( std::dynamic_pointer_cast<lt::plugin>( session ) );
    session->session()->lt_session().m_dbgOurPeerName = dbgClientName;
    return session;
}

}

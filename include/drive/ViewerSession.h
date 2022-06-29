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

using DownloadStreamProgress = std::function<void( std::string playListPath, int chunkIndex, int chunkNumber, std::string error )>;

class ViewerSession : public ClientSession, public DhtMessageHandler, public lt::plugin
{
    struct ViewerChunkInfo : public ChunkInfo
    {
        uint32_t   m_offsetMs;
        bool       m_isDownloaded = false;
        
        ViewerChunkInfo( const ChunkInfo& info, uint32_t  offsetMs ) : ChunkInfo(info), m_offsetMs(offsetMs) {}
    };

    std::optional<Hash256>  m_streamId;     // tx hash
    Key                     m_streamerKey;  // streamer public key
    Key                     m_driveKey;
    bool                    m_streamFinished = false;
    
    endpoint_list           m_replicatorEndpointList;

    using udp_endpoint_list = std::set<boost::asio::ip::udp::endpoint>;
    udp_endpoint_list       m_udpReplicatorEndpointList;
    
    std::set<std::array<uint8_t,32>> m_replicatorSet;
    
    fs::path                m_chunkFolder;
    fs::path                m_torrentFolder;
    
    using ChunkInfoList = std::deque<ViewerChunkInfo>;
    ChunkInfoList           m_chunkInfoList;

    
    std::optional<lt_handle>    m_downloadingLtHandle;
    uint32_t                    m_tobeDownloadedChunkIndex = 0;

    // playlistInfoHashMap
    std::map< boost::asio::ip::udp::endpoint, InfoHash > m_playlistInfoHashMap;
    bool                                                 m_playlistInfoHashReceived = false;
    
    // Downloading saved/finished stream
    std::optional< InfoHash >               m_downloadStreamPlaylistInfoHash;
    fs::path                                m_downloadStreamDestFolder;
    std::optional< DownloadStreamProgress > m_downloadStreamProgress;
    std::deque<InfoHash>                    m_downloadStreamChunks;
    std::deque<InfoHash>::iterator          m_downloadStreamChunksIt;
    int                                     m_downloadStreamChunkIndex;

    const std::string           m_dbgOurPeerName;
    
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
        
        if ( m_streamFinished )
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
        archive( m_streamId->array() );
        archive( chunkIndex );

        for( auto& endpoint : m_udpReplicatorEndpointList )
        {
            m_session->sendMessage( "get-chunks-info", endpoint, os.str() );
        }
    }
    
    void startWatchingLiveStream( const Hash256&          streamId,
                                  const Key&              streamerKey,
                                  const Key&              driveKey,
                                  const fs::path&         workFolder,
                                  const endpoint_list&    replicatorEndpointList,
                                  DownloadStreamProgress  downloadStreamProgress )
    {
        _ASSERT( ! m_streamId )
        _ASSERT( replicatorEndpointList.size() > 0 )

        m_streamId = streamId;
        m_streamerKey = streamerKey;
        m_driveKey = driveKey;
        m_downloadStreamProgress = downloadStreamProgress;
        m_replicatorEndpointList = replicatorEndpointList;

        for( const auto& endpoint : replicatorEndpointList )
        {
            m_udpReplicatorEndpointList.emplace( endpoint.address(), endpoint.port() );
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
        _LOG("updatePlaylist: " << lastChunkIndex )
        
        const uint32_t maxChunkNumber = 4;
        uint32_t firstChunkNumber;
        uint32_t totalChunkNumber;

        if ( m_chunkInfoList.size() > maxChunkNumber )
        {
            firstChunkNumber = uint32_t(m_chunkInfoList.size()) - maxChunkNumber;
            totalChunkNumber = maxChunkNumber;
        }
        else
        {
            firstChunkNumber = 0;
            totalChunkNumber = uint32_t(m_chunkInfoList.size());
        }
        
        std::stringstream playlist;
        playlist << "#EXTM3U" << std::endl;
        playlist << "#EXT-X-VERSION:3" << std::endl;

        uint32_t maxDuration = 1;
        for( uint32_t i=0; i<totalChunkNumber; i++ )
        {
            uint32_t seconds = (m_chunkInfoList[firstChunkNumber+i].m_durationMks+100000-1)/1000000;
            if ( maxDuration < seconds )
            {
                maxDuration = seconds;
            }
        }

        playlist << "#EXT-X-TARGETDURATION:" << maxDuration << std::endl;
        playlist << "#EXT-X-MEDIA-SEQUENCE:" << firstChunkNumber << std::endl;
        
        for( uint32_t i=0; i<totalChunkNumber; i++ )
        {
            playlist << "#EXTINF:" << m_chunkInfoList[firstChunkNumber+i].m_durationMks/1000000 << "." << m_chunkInfoList[i].m_durationMks%1000000 << std::endl;
            playlist << Key(m_chunkInfoList[firstChunkNumber+i].m_chunkInfoHash) << std::endl;
        }
        
        _LOG( "updatePlaylist: " << firstChunkNumber << " " <<  totalChunkNumber )
        
        auto playlistTxt = playlist.str();
        std::ofstream fileStream( fs::path(m_chunkFolder) / PLAYLIST_FILE_NAME, std::ios::binary );
        fileStream.write( playlistTxt.c_str(), playlistTxt.size() );
    }
    
    void handleDhtResponse( lt::bdecode_node response, boost::asio::ip::udp::endpoint endpoint ) override
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
                
                if ( chunkIndex == 0xffffFFFF )
                {
                    // stream maybe is ended
                    if ( ! m_playlistInfoHashReceived )
                    {
                        requestPlaylistInfoHash();
                    }
                    return;
                }
                else if ( chunkIndex == 0xffffFFF0 )
                {
                    // wait stream start
                    return;
                }

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

            if ( query == "get-playlist-hash" )
            {
                if ( m_playlistInfoHashReceived )
                {
                    return;
                }

                lt::string_view ret = rDict.dict_find_string_value("ret");
                std::string result( ret.begin(), ret.end() );

                std::istringstream is( result, std::ios::binary );
                cereal::PortableBinaryInputArchive iarchive(is);

                std::array<uint8_t,32> streamId;
                iarchive( streamId );
                if ( streamId != m_streamId )
                {
                    // ignore bad reply
                    return;
                }
                
                if ( m_udpReplicatorEndpointList.find(endpoint) == m_udpReplicatorEndpointList.end() )
                {
                    return;
                }
                
                std::array<uint8_t,32> playlistInfoHash;
                iarchive( playlistInfoHash );
                m_playlistInfoHashMap[endpoint] = InfoHash(playlistInfoHash);
                
                if ( m_playlistInfoHashMap.size() > (m_udpReplicatorEndpointList.size()*2)/3 + 1 )
                {
                    struct VoteCounter { int counter = 0; };
                    std::map<InfoHash,VoteCounter> votingMap;
                    
                    for( auto& pair : m_playlistInfoHashMap )
                    {
                        size_t voteNumber = votingMap[pair.second].counter++;
                        if ( voteNumber >= (m_udpReplicatorEndpointList.size()*2)/3 + 1 )
                        {
                            m_playlistInfoHashReceived = true;
                            startDownloadFinishedStream( pair.second, m_chunkFolder );
                            return;
                        }
                    }
                }
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
    
    void requestPlaylistInfoHash()
    {
        std::ostringstream os( std::ios::binary );
        cereal::PortableBinaryOutputArchive archive( os );
        
        archive( m_driveKey.array() );
        archive( m_streamId->array() );

        for( auto& endpoint : m_udpReplicatorEndpointList )
        {
            //todo filter end-of-stream-peers
            m_session->sendMessage( "get-playlist-hash", endpoint, os.str() );
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
        
        if ( ! m_downloadingLtHandle && m_downloadStreamPlaylistInfoHash )
        {
            downloadPlaylist();
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
                                   DownloadContext::stream_data,

                                   [this]( download_status::code code,
                                           const InfoHash& infoHash,
                                           const std::filesystem::path saveAs,
                                           size_t downloadedSize,
                                           size_t /*fileSize*/,
                                           const std::string& errorText )
                                   {
                                       if ( code == download_status::download_complete )
                                       {
                                           m_chunkInfoList[m_tobeDownloadedChunkIndex-1].m_isDownloaded = true;
                                           updatePlaylist( m_tobeDownloadedChunkIndex-1 );
                                           (*m_downloadStreamProgress)( m_chunkFolder / PLAYLIST_FILE_NAME, m_tobeDownloadedChunkIndex-1, m_tobeDownloadedChunkIndex, {} );
                                           m_downloadingLtHandle.reset();
                                           tryLoadNextChunk();
                                       }
                                       else if ( code == download_status::dn_failed )
                                       {
                                           (*m_downloadStreamProgress)( m_chunkFolder / PLAYLIST_FILE_NAME, m_tobeDownloadedChunkIndex-1, m_tobeDownloadedChunkIndex, "download failed" );
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
                           &m_streamId->array(),
                           m_replicatorEndpointList
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
    
    void setDownloadChannel( const ReplicatorList& replicatorList, Hash256 downloadChannelId, std::vector<uint64_t> alreadyReceivedSize = {} )
    {
        for( const auto& replicatorKey : replicatorList )
        {
            m_replicatorSet.insert( replicatorKey.array() );
        }
        
        ClientSession::setDownloadChannel( replicatorList, downloadChannelId, alreadyReceivedSize );
    }

    bool onPieceRequestReceivedFromReplicator( const std::array<uint8_t,32>&  transactionHash,
                                               const std::array<uint8_t,32>&  receiverPublicKey,
                                               uint64_t                       pieceSize ) override
    {
        if ( m_replicatorSet.find( receiverPublicKey ) != m_replicatorSet.end() )
        {
            //_LOG( "receiverPublicKey: " << Key(receiverPublicKey) )
            // do not resend chunks to replicators
            return false;
        }
        return true;
    }

    bool onPieceRequestReceivedFromClient( const std::array<uint8_t,32>&  transactionHash,
                                           const std::array<uint8_t,32>&  receiverPublicKey,
                                           uint64_t                       pieceSize ) override
    {
        return true;
    }

//    void startWatchingStream( const Hash256&          streamId,
//                              const Key&              driveKey,
//                              const fs::path&         destFolder,
//                              const endpoint_list&    replicatorEndpointList,
//                              DownloadStreamProgress  downloadStreamProgress )
//    {
//        _ASSERT( ! m_streamId )
//
//        if ( m_downloadStreamProgress )
//        {
//            _LOG_WARN( "cannot start download stream before previous stream download is not ended" )
//            throw std::runtime_error( "cannot start download stream before previous stream download is not ended" );
//        }
//
//        m_streamId = streamId;
//        m_driveKey = driveKey;
//        m_downloadStreamProgress = downloadStreamProgress;
//
//        requestPlaylistInfoHash();
//    }
    
    void startDownloadFinishedStream( InfoHash              playlistInfoHash,
                                      std::filesystem::path destFolder )
    {
        m_downloadStreamPlaylistInfoHash = playlistInfoHash;
        m_downloadStreamDestFolder       = destFolder;

        boost::asio::post( m_session->lt_session().get_context(), [this]() //mutable
        {
            if ( m_downloadingLtHandle )
            {
                _LOG( "waiting m_downloadingLtHandle.reset()" )
                return;
            }
            
            downloadPlaylist();
        });
    }
    
    void downloadPlaylist()
    {
        m_downloadingLtHandle = m_session->download(
                           DownloadContext(
                                   DownloadContext::stream_data,

                                   [this]( download_status::code code,
                                           const InfoHash& infoHash,
                                           const std::filesystem::path saveAs,
                                           size_t downloadedSize,
                                           size_t /*fileSize*/,
                                           const std::string& errorText )
                                   {
                                       if ( code == download_status::download_complete )
                                       {
                                           auto playlistFilePath = m_downloadStreamDestFolder / PLAYLIST_FILE_NAME;
                                           std::error_code err;
                                           if ( fs::exists( playlistFilePath, err ) )
                                           {
                                               fs::remove( playlistFilePath, err );
                                           }
                                           fs::rename( m_downloadStreamDestFolder / toString(*m_downloadStreamPlaylistInfoHash),
                                                       playlistFilePath,
                                                       err );

                                           m_downloadingLtHandle.reset();
                                           m_downloadStreamPlaylistInfoHash.reset();
                                           downloadChunks();
                                       }
                                       else if ( code == download_status::dn_failed )
                                       {
                                       }
                                   },

                                   *m_downloadStreamPlaylistInfoHash,
                                   *m_streamId,
                                   0,
                                   false, {}
                           ),

                           m_downloadStreamDestFolder,
                           {},
                           {},
                           &m_driveKey.array(),
                           &(*m_downloadChannelId),
                           &m_streamId->array(),
                           m_replicatorEndpointList
                        );
    }
    
    void downloadChunks()
    {
        try
        {
        m_downloadStreamChunks.clear();
        
        std::ifstream fin( m_downloadStreamDestFolder / PLAYLIST_FILE_NAME );
        std::stringstream fPlaylist;
        fPlaylist << fin.rdbuf();
        
        std::string line;
        
        if ( ! std::getline( fPlaylist, line ) || memcmp( line.c_str(), "#EXTM3U", 7 ) != 0 )
        {
            _LOG_WARN( "1-st line of playlist must be '#EXTM3U'" );
            //todo? display error by UI
        }
        
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
                    _LOG_WARN( std::string("invalid playlist format: ") + line );
                    return;
                }
                
                if ( version != 3 && version != 4 )
                {
                    _LOG_WARN( std::string("invalid version number: ") + line.substr(15) );
                    return;
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
                    std::stoi( line.substr(16+6) );
                }
                catch(...)
                {
                    _LOG_ERR( std::string("cannot read sequence number: ") + line );
                    return;
                }
                continue;
            }

            if ( memcmp( line.c_str(), "#EXTINF:", 8 ) == 0 )
            {
                try
                {
                    std::stof( line.substr(8) );
                }
                catch(...)
                {
                    _LOG_WARN( std::string("cannot read duration attribute: ") + line );
                }
                
                if ( ! std::getline( fPlaylist, line ) )
                {
                    break;
                }
                
                _LOG( line );
                m_downloadStreamChunks.emplace_back( stringToHash(line) );

                continue;
            }
        }
            
        m_downloadStreamChunksIt = m_downloadStreamChunks.begin();
        m_downloadStreamChunkIndex = -1;
        continueDownloadChunks();
            
        }
        catch( const std::exception& e )
        {
            //todo? display error by UI
            _LOG_WARN( "downloadChunks: exception: " << e.what() )
        }
        catch( ... )
        {
            _LOG_ERR( "downloadChunks: unknown exception" )
        }
    }
    
    void continueDownloadChunks()
    {
download_next_chunk:

        if ( m_downloadStreamChunkIndex >= 0 )
        {
            m_downloadStreamChunksIt++;
        }

        m_downloadStreamChunkIndex++;

        if ( m_downloadStreamChunksIt == m_downloadStreamChunks.end() )
        {
            (*m_downloadStreamProgress)( m_downloadStreamDestFolder / PLAYLIST_FILE_NAME, m_downloadStreamChunkIndex, int(m_downloadStreamChunks.size()), {} );
            return;
        }
        
        auto it = std::find_if( m_chunkInfoList.begin(), m_chunkInfoList.end(), [this](const auto& chunkInfo) { return chunkInfo.m_chunkInfoHash == m_downloadStreamChunksIt->array(); } );
        if (  it != m_chunkInfoList.end() && it->m_isDownloaded )
        {
//            if ( m_chunkFolder != m_downloadStreamDestFolder )
//            {
//                std::error_code err;
//                fs::copy( m_chunkFolder / toString( *m_downloadStreamChunksIt ), m_downloadStreamDestFolder / toString( *m_downloadStreamChunksIt ), err );
//            }

//            (*m_downloadStreamProgress)( m_downloadStreamDestFolder / PLAYLIST_FILE_NAME, m_downloadStreamChunkIndex, int(m_downloadStreamChunks.size()), {} );

            goto download_next_chunk;
        }
        
        m_downloadingLtHandle = m_session->download(
                           DownloadContext(
                                   DownloadContext::stream_data,

                                   [this]( download_status::code code,
                                           const InfoHash& infoHash,
                                           const std::filesystem::path saveAs,
                                           size_t downloadedSize,
                                           size_t /*fileSize*/,
                                           const std::string& errorText )
                                   {
                                       if ( code == download_status::download_complete )
                                       {
                                           m_downloadingLtHandle.reset();
                                           (*m_downloadStreamProgress)( m_downloadStreamDestFolder / PLAYLIST_FILE_NAME, m_downloadStreamChunkIndex, int(m_downloadStreamChunks.size()), {} );
                                           continueDownloadChunks();
                                       }
                                       else if ( code == download_status::dn_failed )
                                       {
                                       }
                                   },

                                   *m_downloadStreamChunksIt,
                                   *m_streamId,
                                   0,
                                   false, {}
                           ),

                           m_chunkFolder,
                           {},
                           {}, //getUploaders(),
                           &m_driveKey.array(),
                           &(*m_downloadChannelId),
                           &m_streamId->array(),
                           m_replicatorEndpointList
                        );
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

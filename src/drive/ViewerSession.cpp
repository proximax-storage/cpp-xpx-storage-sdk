/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "drive/Session.h"
#include "drive/Streaming.h"
#include "http_server.hpp"
#include "drive/ViewerSession.h"
#include "drive/StreamerSession.h"
#include "drive/log.h"
#include "drive/Utils.h"
#include "crypto/Signer.h"

#include <boost/algorithm/hex.hpp>

#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/extensions.hpp"
#include <sirius_drive/session_delegate.h>

#include <iostream>
#include <fstream>

// 'ViewSession' extends 'StreamerSession' and 'ClientSession'
// It is created because 'libtorrent session' must be uninque
// So we use such approach instead of sharing 'libtorrent session' (std::shared_ptr<>)

// Streamer every second do 'parseM3u8Playlist()'
// If playlist changed, Streamer sends message 'chunk-info' to all Replicators

// Viewer every second send message "get-chunks-info" to all Replicators - requestChunkInfo()
// When array of newest ChunkInfo received:
//  1) generates playlist - updatePlaylist()
//  2) starts chunk downloading

// ??? Must be removed???
// Sometimes Replicators send message "get-chunk-info" to Streamer (to speed up downloading of missed chunks?)

namespace sirius::drive {

class DefaultViewerSession : public ViewerSession
{
    struct ViewerChunkInfo : public ChunkInfo
    {
        uint32_t   m_offsetMs;
        bool       m_isDownloaded = false;

        ViewerChunkInfo( const ChunkInfo& info, uint32_t  offsetMs ) : ChunkInfo(info), m_offsetMs(offsetMs) {}
    };

    std::optional<Hash256>  m_liveStreamId;
    bool                    m_liveStreamFinished = false;

    Key                     m_streamerKey;  // live-streamer public key

    // it is defined in StreamerSession:
    //Key                     m_driveKey;

    std::array<uint8_t,32>  m_downloadChannelId;

    ReplicatorList          m_replicatorList;

    std::set<std::array<uint8_t,32>> m_replicatorSet;

    // they are defined in StreamerSession:
//    fs::path                m_chunkFolder;
//    fs::path                m_torrentFolder;
    fs::path                m_relativeChunkFolder;

    using ChunkInfoList = std::deque<ViewerChunkInfo>;
    ChunkInfoList           m_chunkInfoList;


    std::optional<lt_handle>    m_downloadingLtHandle;
    uint32_t                    m_tobeDownloadedChunkIndex = 0;

    // playlistInfoHashMap
    std::map< boost::asio::ip::udp::endpoint, InfoHash > m_playlistInfoHashMap;
    bool                                                 m_playlistInfoHashReceived = false;

    // Downloading saved/finished stream
    std::optional< InfoHash >               m_downloadStreamPlaylistInfoHash;
    fs::path                                m_streamRootFolder;                 // it is root folder of http/hls server
    fs::path                                m_finishedStreamDestFolder;
    std::optional< DownloadStreamProgress > m_downloadStreamProgress;
    std::optional< StartPlayerMethod >      m_startPlayerMethod;
    HttpServerParams                        m_httpServerParams;
    std::deque<InfoHash>                    m_downloadStreamChunks;
    std::deque<InfoHash>::iterator          m_downloadStreamChunksIt;
    int                                     m_downloadStreamChunkIndex;

    std::unique_ptr<http::server::server>   m_httpServer;
    
    std::optional<StreamStatusResponseHandler>  m_streamStatusResponseHandler;

    const std::string           m_dbgOurPeerName;

public:
    DefaultViewerSession( const crypto::KeyPair& keyPair, const char* dbgOurPeerName )
        :
            ViewerSession( keyPair, dbgOurPeerName ),
            m_dbgOurPeerName(dbgOurPeerName)
    {
    }

    ~DefaultViewerSession()
    {
    }

    feature_flags_t implemented_features() override
    {
        return plugin::tick_feature;
    }

    void on_tick() override
    {
        if ( ! m_liveStreamId )
        {
            return;
        }

        if ( m_liveStreamFinished )
        {
            return;
        }

        requestChunkInfo( uint32_t(m_chunkInfoList.size()) );
    }

    void startWatchingLiveStream( const Hash256&          streamId,
                                  const Key&              streamerKey,
                                  const Key&              driveKey,
                                  const Hash256&          channelId,
                                  const ReplicatorList&   replicators,
                                  const fs::path&         streamRootFolder,
                                  const fs::path&         streamFolder,     // folder inside 'streamRootFolder' where stream will be saved
                                  StartPlayerMethod       startPlayerMethod,
                                  HttpServerParams        httpServerParams,
                                  DownloadStreamProgress  downloadStreamProgress ) override
    {
        if ( m_liveStreamId )
        {
            SIRIUS_ASSERT( *m_liveStreamId  == streamId );
            return;
        }

        m_streamerKey            = streamerKey;
        m_driveKey               = driveKey;
        m_downloadChannelId      = channelId.array();
        if ( ! m_httpServer )
        {
            m_streamRootFolder   = streamRootFolder;
        }
        m_downloadStreamProgress = downloadStreamProgress;
        m_startPlayerMethod      = startPlayerMethod;
        m_httpServerParams       = httpServerParams;
        m_replicatorList         = replicators;

        for( const auto& replicatorKey : replicators )
        {
            m_replicatorSet.insert( replicatorKey.array() );
        }
        
        addDownloadChannel(channelId);
        setDownloadChannelReplicators(channelId, replicators);

        fs::remove_all( streamFolder );
        m_relativeChunkFolder   = streamFolder / "chunks";
        m_chunkFolder           = m_streamRootFolder / m_relativeChunkFolder;
        m_torrentFolder         = m_streamRootFolder / streamFolder / "torrents";

        fs::create_directories( m_chunkFolder );
        fs::create_directories( m_torrentFolder );

        if ( ! m_httpServer )
        {
            try
            {
                m_httpServer = std::make_unique<http::server::server>( m_httpServerParams.m_address, m_httpServerParams.m_port, m_streamRootFolder.string() );
                std::thread( [this] { m_httpServer->run(); } ).detach();
            }
            catch( const std::runtime_error& err )
            {
                _LOG_WARN( "httpServer error: " << err.what() );
                (*m_downloadStreamProgress)( (m_chunkFolder / PLAYLIST_FILE_NAME).string(), 0, 1, err.what() );
            }
        }
        
        m_liveStreamId = streamId;
    }

    void endWatching()
    {
        m_liveStreamId.reset();
        //TODO
    }

    void requestChunkInfo( uint32_t chunkIndex )
    {
        SIRIUS_ASSERT( m_liveStreamId )

        std::ostringstream os( std::ios::binary );
        cereal::PortableBinaryOutputArchive archive( os );

        archive( m_driveKey.array() );
        archive( m_liveStreamId->array() );
        archive( chunkIndex );

        for( auto& replicatorKey : m_replicatorSet )
        {
            auto endpoint = m_session->getEndpoint( replicatorKey );
            if ( endpoint )
            {
                boost::asio::ip::udp::endpoint udpEndpoint( endpoint.value().address(), endpoint.value().port() );
                m_session->sendMessage( "get-chunks-info", udpEndpoint, os.str() );
            }
        }
    }

    void requestStreamStatus( const std::array<uint8_t,32>& driveKey,
                              const sirius::drive::ReplicatorList& replicatorKeys,
                              StreamStatusResponseHandler streamStatusResponseHandler ) override
    {
        m_streamStatusResponseHandler = streamStatusResponseHandler;
        
        std::ostringstream os( std::ios::binary );
        cereal::PortableBinaryOutputArchive archive( os );

        archive( driveKey );

        for( auto& replicatorKey : replicatorKeys )
        {
            auto endpoint = m_session->getEndpoint( replicatorKey );
            if ( endpoint )
            {
                boost::asio::ip::udp::endpoint udpEndpoint( endpoint.value().address(), endpoint.value().port() );
                
                //_LOG( "sendMessage: get_stream_status: " << udpEndpoint );
                m_session->sendMessage( "get_stream_status", udpEndpoint, os.str() );
            }
        }
    }

    void updatePlaylist( uint32_t lastChunkIndex )
    {
        _LOG("updatePlaylist: " << lastChunkIndex )

        const uint32_t maxChunkNumber = 7;
        uint32_t firstChunkNumber = lastChunkIndex;
        uint32_t totalChunkNumber = 1;

        while( totalChunkNumber <= maxChunkNumber )
        {
            if ( firstChunkNumber == 0 )
                break;

            firstChunkNumber--;

            if ( ! m_chunkInfoList[firstChunkNumber].m_isDownloaded )
                break;

            totalChunkNumber++;
        }

        ////
//        firstChunkNumber = 0;
//        totalChunkNumber = lastChunkIndex+1;

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

        {
            auto playlistTxt = playlist.str();
            std::ofstream fileStream( fs::path(m_chunkFolder) / PLAYLIST_FILE_NAME, std::ios::binary );
            fileStream.write( playlistTxt.c_str(), playlistTxt.size() );
        }

        startPlayer();
    }

    void startPlayer()
    {
        if ( m_startPlayerMethod )
        {
//            std::string address = "http://" + m_httpServerParams.m_address + ":" + m_httpServerParams.m_port + "/" + (m_relativeChunkFolder / PLAYLIST_FILE_NAME).string();
            std::string address = (m_streamRootFolder / m_relativeChunkFolder / PLAYLIST_FILE_NAME).string();

            (*m_startPlayerMethod)( address );
            m_startPlayerMethod.reset();
        }
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
                
                _LOG( "result.size: " << result.size() );
                std::string hexString;
                boost::algorithm::hex( result.begin(), result.end(), std::back_inserter(hexString) );
                _LOG( "result.hexString: " << hexString );


                std::istringstream is( result, std::ios::binary );
                cereal::PortableBinaryInputArchive iarchive(is);

                uint32_t  chunkIndex;
                iarchive( chunkIndex );

                _LOG( "@@@ get-chunks-info: chunkIndex: " << chunkIndex );

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
                    
                    _LOG( "sss+ chunkIndex: " << info.m_chunkIndex )
                    _LOG( "sss+ streamId: " << toString(info.m_streamId) )
                    _LOG( "sss+ hash: " << toString(info.m_chunkInfoHash) )
                    
                    if ( ! addChunkInfo( info ) )
                    {
                        return;
                    }
                }

                tryLoadNextChunk();
                return;
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

                SIRIUS_ASSERT( m_liveStreamId )

                if ( streamId != m_liveStreamId->array() )
                {
                    // ignore bad reply
                    _LOG_WARN( "streamId != m_liveStreamId" )
                    return;
                }

                std::array<uint8_t,32> playlistInfoHash;
                iarchive( playlistInfoHash );
                m_playlistInfoHashMap[endpoint] = InfoHash(playlistInfoHash);

                if ( m_playlistInfoHashMap.size() > (m_replicatorSet.size()*2)/3 + 1 )
                {
                    struct VoteCounter { int counter = 0; };
                    std::map<InfoHash,VoteCounter> votingMap;

                    for( auto& pair : m_playlistInfoHashMap )
                    {
                        size_t voteNumber = votingMap[pair.second].counter++;
                        _LOG( "@@@ voteNumber: " << voteNumber )
                        if ( voteNumber >= (m_replicatorSet.size()*2)/3 + 1 )
                        {
                            m_playlistInfoHashReceived = true;
                            startDownloadFinishedStream( pair.second, m_chunkFolder );
                            return;
                        }
                    }
                }
                return;
            }

            if ( query == "get_stream_status" )
            {
                if ( ! m_streamStatusResponseHandler )
                {
                    return;
                }

                lt::string_view ret = rDict.dict_find_string_value("ret");
                std::string result( ret.begin(), ret.end() );

                std::istringstream is( result, std::ios::binary );
                cereal::PortableBinaryInputArchive iarchive(is);

                std::array<uint8_t,32> driveKey;
                iarchive( driveKey );
                _LOG( "get_stream_status: driveKey: " << toString(driveKey) )

                std::array<uint8_t,32> streamerKey;
                iarchive( streamerKey );
                _LOG( "get_stream_status: streamerKey: " << toString(streamerKey) )

                //TODO!!!!!!!!!!!
                uint16_t isStreaming = true;
                iarchive( isStreaming );
                _LOG( "get_stream_status: isStreaming: " << int(isStreaming) )

                std::array<uint8_t,32> streamId;
                iarchive( streamId );
                _LOG( "get_stream_status: streamId: " << toString(streamId) )

                (*m_streamStatusResponseHandler) ( driveKey, streamerKey, isStreaming!=0, streamId );
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
        
        ClientSession::handleDhtResponse( response, endpoint );
    }

    void requestPlaylistInfoHash()
    {
        std::ostringstream os( std::ios::binary );
        cereal::PortableBinaryOutputArchive archive( os );

        archive( m_driveKey.array() );
        archive( m_liveStreamId->array() );

        for( auto& replicatorKey : m_replicatorSet )
        {
            auto endpoint = m_session->getEndpoint( replicatorKey );
            if ( endpoint )
            {
                boost::asio::ip::udp::endpoint udpEndpoint( endpoint.value().address(), endpoint.value().port() );
                m_session->sendMessage( "get-playlist-hash", udpEndpoint, os.str() );
            }
        }
    }

    bool addChunkInfo( const ChunkInfo& info )
    {
        uint32_t newIndex = uint32_t(m_chunkInfoList.size());
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
        if ( ! m_downloadingLtHandle && m_downloadStreamPlaylistInfoHash )
        {
            downloadPlaylist();
        }

        if ( m_downloadingLtHandle || m_chunkInfoList.size() <= m_tobeDownloadedChunkIndex )
        {
            return;
        }

        const auto& chunkInfo = m_chunkInfoList[m_tobeDownloadedChunkIndex];
        SIRIUS_ASSERT( m_tobeDownloadedChunkIndex == chunkInfo.m_chunkIndex )
        m_tobeDownloadedChunkIndex++;

        _LOG( "@@@ download: " << chunkInfo.m_chunkIndex  << " :" << InfoHash(chunkInfo.m_chunkInfoHash) )

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
                                           _LOG( "@@@ downloaded: " << m_tobeDownloadedChunkIndex-1 )
                                           m_chunkInfoList[m_tobeDownloadedChunkIndex-1].m_isDownloaded = true;
                                           updatePlaylist( m_tobeDownloadedChunkIndex-1 );
                                           (*m_downloadStreamProgress)( (m_chunkFolder / PLAYLIST_FILE_NAME).string(), m_tobeDownloadedChunkIndex-1, m_tobeDownloadedChunkIndex, {} );
                                           m_downloadingLtHandle.reset();
                                           tryLoadNextChunk();
                                       }
                                       else //if ( code == download_status::dn_failed )
                                       {
                                           _LOG( "@@@ download failed: " << m_tobeDownloadedChunkIndex-1 << " :" << infoHash )
                                           (*m_downloadStreamProgress)( (m_chunkFolder / PLAYLIST_FILE_NAME).string(), m_tobeDownloadedChunkIndex-1, m_tobeDownloadedChunkIndex, "download failed" );
                                       }
                                   },

                                   InfoHash(chunkInfo.m_chunkInfoHash),
                                   *m_liveStreamId,
                                   0,
                                   true, {}
                           ),

                           m_chunkFolder.string(),
                           (m_torrentFolder / (toString(InfoHash(chunkInfo.m_chunkInfoHash)))).string(),
                           m_replicatorList,
                           &m_driveKey.array(),
                           &(m_downloadChannelId),
                           &m_liveStreamId->array()
                        );
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

    void startDownloadFinishedStream( InfoHash              playlistInfoHash,
                                      std::filesystem::path destFolder )
    {
        m_downloadStreamPlaylistInfoHash = playlistInfoHash;
        m_finishedStreamDestFolder       = destFolder;

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
                                           auto playlistFilePath = m_finishedStreamDestFolder / PLAYLIST_FILE_NAME;
                                           std::error_code err;
                                           if ( fs::exists( playlistFilePath, err ) )
                                           {
                                               fs::remove( playlistFilePath, err );
                                           }
                                           fs::rename( m_finishedStreamDestFolder / toString(*m_downloadStreamPlaylistInfoHash),
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
                                   *m_liveStreamId,
                                   0,
                                   false, {}
                           ),

                           m_finishedStreamDestFolder.string(),
                           {},
                           m_replicatorList,
                           &m_driveKey.array(),
                           &(m_downloadChannelId),
                           &m_liveStreamId->array()
                        );
    }

    void downloadChunks()
    {
        try
        {
        m_downloadStreamChunks.clear();

        std::ifstream fin( m_finishedStreamDestFolder / PLAYLIST_FILE_NAME );
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

                //_LOG( line );
                m_downloadStreamChunks.emplace_back( stringToByteArray<Hash256>(line) );

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
            (*m_downloadStreamProgress)( (m_finishedStreamDestFolder / PLAYLIST_FILE_NAME).string(), m_downloadStreamChunkIndex, int(m_downloadStreamChunks.size()), {} );
            return;
        }

        auto it = std::find_if( m_chunkInfoList.begin(), m_chunkInfoList.end(), [this](const auto& chunkInfo) { return chunkInfo.m_chunkInfoHash == m_downloadStreamChunksIt->array(); } );
        if (  it != m_chunkInfoList.end() && it->m_isDownloaded )
        {
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
                                           (*m_downloadStreamProgress)( (m_finishedStreamDestFolder / PLAYLIST_FILE_NAME).string(), m_downloadStreamChunkIndex, int(m_downloadStreamChunks.size()), {} );
                                           startPlayer();
                                           continueDownloadChunks();
                                       }
                                       else if ( code == download_status::dn_failed )
                                       {
                                       }
                                   },

                                   *m_downloadStreamChunksIt,
                                   *m_liveStreamId,
                                   0,
                                   false, {}
                           ),

                           m_chunkFolder.string(),
                           {},
                           m_replicatorList,
                           &m_driveKey.array(),
                           &(m_downloadChannelId),
                           &m_liveStreamId->array()
                        );
    }
};

std::shared_ptr<ViewerSession> createViewerSession( const crypto::KeyPair&                      keyPair,
                                                           const std::string&                   address,
                                                           const LibTorrentErrorHandler&        errorHandler,
                                                           const std::vector<ReplicatorInfo>&   bootstraps,
                                                           bool                                 useTcpSocket, // instead of uTP
                                                           const char*                          dbgClientName )
{
    std::shared_ptr<DefaultViewerSession> session = std::make_shared<DefaultViewerSession>( keyPair, dbgClientName );
    session->session() = createDefaultSession( address, keyPair, errorHandler, session, bootstraps, session );
    session->session()->lt_session().add_extension( std::dynamic_pointer_cast<lt::plugin>( session ) );
    session->session()->lt_session().m_dbgOurPeerName = dbgClientName;
    
    //TODO?
    //session->addDownloadChannel(Hash256{});

    return session;
}

}

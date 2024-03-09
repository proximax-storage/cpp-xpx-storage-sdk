/*
*** Copyright 2022 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "drive/Streaming.h"

namespace sirius::drive
{

namespace fs = std::filesystem;

class StreamTask : public ModifyDriveTask
{
    std::mutex  m_mutex;
    
    const uint32_t MAX_CHUNKS_DURATION_MS = 10000;
    std::unique_ptr<StreamRequest>  m_request;
    
    // 'ChunkInfo' are sent by 'StreamerClient' via DHT message
    // They are saved in 'm_chunkInfoList' (ordered by m_chunkIndex)
    //
    // Chunks are downloaded one by one
    // 'm_downloadingChunkInfoIt' is a refference to current downloading (or last downloaded) 'ChunkInfo'
    //
    // If some 'ChunkInfo' message is lost, it will be requested after the lost is detected
    //
    
    using ChunkInfoList = std::deque< std::unique_ptr<ChunkInfo> >;
    ChunkInfoList                   m_chunkInfoList;
    ChunkInfoList::iterator         m_downloadingChunkInfoIt;
    bool                            m_downloadingChunkInfoItWasSet = false;
    
    std::optional<boost::asio::ip::udp::endpoint>  m_streamerEndpoint;

    std::optional<InfoHash>         m_finishDataInfoHash;

    
public:
    
    StreamTask(  mobj<StreamRequest>&&       request,
                 DriveParams&                drive,
                 ModifyOpinionController&    opinionTaskController )
            :
    ModifyDriveTask( drive, opinionTaskController )
            , m_request( std::move(request) )
    {
        SIRIUS_ASSERT( m_request )
        _LOG( "StreamTask: streamId: " << request->m_streamId )
        _LOG( "StreamTask: folder:   " << request->m_folder )

    }
    
    ~StreamTask()
    {
        _LOG( "m_chunkInfoList.size: " << m_chunkInfoList.size() )
    }
    
    void onCancelModifyTx( const ModificationCancelRequest& cancelRequest, bool& cancelRequestIsAccepted ) override
    {
        DBG_MAIN_THREAD
        
        //TODO== m_finishInfoHash
        _LOG( "m_taskIsInterrupted:                   " << m_taskIsInterrupted )
        _LOG( "cancelRequest.m_modifyTransactionHash: " << cancelRequest.m_modifyTransactionHash )
        _LOG( "m_request->m_streamId:                 " << m_request->m_streamId )

        if ( ! m_taskIsInterrupted &&
             cancelRequest.m_modifyTransactionHash == m_request->m_streamId )
        {
            interruptTorrentDownloadAndRunNextTask();
            cancelRequestIsAccepted = true;
            return;
        }

        cancelRequestIsAccepted = false;
    }

    void tryFinishTask() override
    {
        DBG_MAIN_THREAD

        //TODO== m_finishInfoHash
    }

    void run() override
    {
        _LOG( "StreamTask::run: m_request->m_streamId: " << m_request->m_streamId )
        _LOG( "StreamTask::run: m_request->m_streamerKey: " << m_request->m_streamerKey )
        _LOG( "StreamTask::run: m_request->m_folder: " << m_request->m_folder )
    }

    const Hash256& getModificationTransactionHash() override
    {
        return m_request->m_streamId;
    }
    
    virtual std::string acceptGetChunksInfoMessage( const std::array<uint8_t,32>&          streamId,
                                                    uint32_t                               requestedIndex,
                                                    const boost::asio::ip::udp::endpoint&  viewer ) override
    {
        DBG_MAIN_THREAD

        if ( m_request->m_streamId != streamId )
        {
            _LOG( "m_request->m_streamId != streamId:" << InfoHash(m_request->m_streamId) << " " << InfoHash(streamId) )
            bool streamFinished = m_drive.m_streamMap.find( Hash256(streamId) ) != m_drive.m_streamMap.end();
            std::ostringstream os( std::ios::binary );
            cereal::PortableBinaryOutputArchive archive( os );
            int32_t streamIsEnded = streamFinished ? 0xffffFFFF : 0xffffFFF0;
            archive( streamIsEnded );

            return os.str();
        }

        if ( m_chunkInfoList.size() <= requestedIndex || m_chunkInfoList[requestedIndex].get() == nullptr )
        {
            // so far we do not have requested chunkInfo (not signed info could be received by finish-stream)
            return "";
        }

        auto chunkInfo = m_chunkInfoList[requestedIndex].get();
            
        if ( chunkInfo == nullptr )
        {
            return "";
        }
        
        SIRIUS_ASSERT( chunkInfo->m_chunkIndex == requestedIndex )
        uint32_t beginIndex = requestedIndex;
        
        // Find the end index (of the chunk sequence)
        //
        const uint32_t MAX_SEQUENCE_SIZE = 5; //?
        uint32_t endIndex = beginIndex+1;
        uint32_t durationMks = 0;

        for( ; endIndex < m_chunkInfoList.size(); endIndex++ )
        {
            if ( endIndex - beginIndex > MAX_SEQUENCE_SIZE )
            {
                break;
            }
            
            auto chunkInfo2 = m_chunkInfoList[endIndex].get();
            if ( chunkInfo2 == nullptr )
            {
                break;
            }

            durationMks += chunkInfo2->m_durationMks;
            if ( durationMks > MAX_CHUNKS_DURATION_MS )
            {
                break;
            }
        }

                
        // Prepare message
        //
        std::ostringstream os( std::ios::binary );
        cereal::PortableBinaryOutputArchive archive( os );
        archive( requestedIndex );
        int32_t chunkNumber = endIndex - requestedIndex;
        archive( chunkNumber );

        for( auto i = beginIndex; i < endIndex; i++ )
        {
            const ChunkInfo& info = * m_chunkInfoList[i].get();
            archive( info );
        }

        return os.str();
    }

    virtual void acceptChunkInfoMessage( mobj<ChunkInfo>&& chunkInfo, const boost::asio::ip::udp::endpoint& sender ) override
    {
        DBG_MAIN_THREAD
        
        SIRIUS_ASSERT( chunkInfo )
        _LOG( "acceptChunkInfoMessage chunk-info: index=" << chunkInfo->m_chunkIndex << " " << toString(chunkInfo->m_chunkInfoHash) )
        
        std::lock_guard<std::mutex> lock(m_mutex);

        if ( chunkInfo->m_streamId != m_request->m_streamId.array() )
        {
            _LOG_WARN( "ignore unkown stream: " << toString(chunkInfo->m_streamId) << " vs. " << m_request->m_streamId )
            return;
        }
        
        if ( ! chunkInfo->Verify( m_request->m_streamerKey ) )
        {
            _LOG_WARN( "Bad sign" )
            return;
        }
        
        m_streamerEndpoint = sender;

        if ( chunkInfo->m_chunkIndex < m_chunkInfoList.size() )
        {
            // we have received lost chunk-info
            m_chunkInfoList[chunkInfo->m_chunkIndex] = std::move(chunkInfo);
        }
        else
        {
            while( chunkInfo->m_chunkIndex > m_chunkInfoList.size() )
            {
                // insert missing null-cells
                m_chunkInfoList.emplace_back( std::unique_ptr<ChunkInfo>(nullptr) );
            }

            // append received info
            m_chunkInfoList.emplace_back( std::move(chunkInfo) );
        }
        
        tryDownloadNextChunk();
    }
    
    virtual std::optional<std::array<uint8_t,32>> getStreamId() override
    {
        if ( m_request )
        {
            return m_request->m_streamId.array();
        }
        return {};
    }

    void requestMissingChunkInfo( uint32_t chunkIndex )
    {
        DBG_MAIN_THREAD

        _LOG( "get-chunk-info: " << chunkIndex )


        if ( ! m_streamerEndpoint )
        {
            if ( auto session = m_drive.m_session.lock(); session )
            {
                m_streamerEndpoint = session->getEndpoint( m_request->m_streamerKey.array() );

                if ( ! m_streamerEndpoint )
                {
                    _LOG_WARN( "no m_streamerEndpoint: " << m_request->m_streamerKey )
                    return;
                }
            }
            else
            {
                _LOG_WARN( "cannot lock m_drive.m_session" )
                return;
            }
        }

        if ( auto session = m_drive.m_session.lock(); session )
        {
            std::ostringstream os( std::ios::binary );
            cereal::PortableBinaryOutputArchive archive( os );
            archive( chunkIndex );

            session->sendMessage( "get-chunk-info", *m_streamerEndpoint, os.str() );
        }
    }
    
    void tryDownloadNextChunk()
    {
        DBG_MAIN_THREAD
        
        if ( m_downloadingLtHandle )
        {
            _LOG( "tryDownloadNextChunk: wait the end of current downloading (1)" )
            // wait the end of current downloading
            return;
        }

        // break thread, for correct message end-processing
        m_drive.executeOnSessionThread( [this]
        {
            if ( m_downloadingLtHandle )
            {
                // wait the end of current downloading
                _LOG( "tryDownloadNextChunk: wait the end of current downloading (2)" )
                return;
            }
            
            if ( m_finishDataInfoHash )
            {
                _LOG( "tryDownloadNextChunk: skip download: we are finishing" )
                return;
            }

            SIRIUS_ASSERT( ! m_downloadingLtHandle )
            
            // select next 'm_downloadingChunkInfoIt'
            //
            if ( ! m_downloadingChunkInfoItWasSet )
            {
                // set 1-st m_downloadingChunkInfoIt
                SIRIUS_ASSERT( ! m_chunkInfoList.empty() )

                if ( m_chunkInfoList.begin()->get() == nullptr )
                {
                    // 1-st ChunkInfo is not received
                    _LOG( "1-st ChunkInfo is not received" )
                    requestMissingChunkInfo( 0 );
                    return;
                }

                m_downloadingChunkInfoItWasSet = true;
                m_downloadingChunkInfoIt = m_chunkInfoList.begin();
            }
            else
            {
                auto next = std::next( m_downloadingChunkInfoIt, 1 );
                if ( next == m_chunkInfoList.end() )
                {
                    // so far, we have nothing to download
                    _LOG( "so far, we have nothing to download" )
                    return;
                }

                if ( next->get() == nullptr )
                {
                    // send request to streamer about missing info
                    _LOG("send request to streamer about missing info")
                    requestMissingChunkInfo( m_downloadingChunkInfoIt->get()->m_chunkIndex+1 );
                    return;
                }

                m_downloadingChunkInfoIt = next;
            }
            
            _LOG("m_downloadingChunkInfoIt: chunkIndex: " << m_downloadingChunkInfoIt->get()->m_chunkIndex << " " << toString(m_downloadingChunkInfoIt->get()->m_chunkInfoHash) )

            // start downloading chunk
            //
            if ( auto session = m_drive.m_session.lock(); session )
            {
                const auto& chunkInfoHash = m_downloadingChunkInfoIt->get()->m_chunkInfoHash;
                
                if ( auto it = m_drive.m_torrentHandleMap.find( chunkInfoHash ); it != m_drive.m_torrentHandleMap.end())
                {
                    _LOG( "already downloaded?" )
                    SIRIUS_ASSERT( it->second.m_ltHandle.is_valid() )
                    tryDownloadNextChunk();
                    return;
                }
                
                m_downloadingLtHandle = session->download(
                       DownloadContext(
                               DownloadContext::missing_files,
                               [this]( download_status::code code,
                                       const InfoHash& infoHash,
                                       const std::filesystem::path saveAs,
                                       size_t /*downloaded*/,
                                       size_t /*fileSize*/,
                                       const std::string& errorText )
                               {
                                   DBG_MAIN_THREAD

                                   SIRIUS_ASSERT( !m_taskIsInterrupted );

                                   if ( code == download_status::dn_failed )
                                   {
                                       //todo is it possible?
                                       SIRIUS_ASSERT( 0 );
                                       m_drive.m_torrentHandleMap.erase( infoHash );
                                       m_downloadingLtHandle.reset();
                                       tryDownloadNextChunk();
                                       return;
                                   }
                                   else if ( code == download_status::dn_not_enougth_space )
                                   {
                                       SIRIUS_ASSERT( 0 );
                                       //todo
//                                       m_drive.m_torrentHandleMap.erase( infoHash );
//                                       modifyIsCompletedWithError( errorText, ModificationStatus::NOT_ENOUGH_SPACE );
                                   }

                                   if ( code == download_status::download_complete )
                                   {
                                       m_downloadingLtHandle.reset();
                                       if ( m_finishDataInfoHash )
                                       {
                                           ModifyDriveTask::startModification();
                                       }
                                       else
                                       {
                                           tryDownloadNextChunk();
                                       }
                                   }
                               },
                               chunkInfoHash,
                               getModificationTransactionHash(),
                               0, true, ""
                       ),
                       m_drive.m_driveFolder,
                       m_drive.m_torrentFolder / toString(chunkInfoHash),
                       getUploaders(),
                       &m_drive.m_driveKey.array(),
                       nullptr,
                       &m_request->m_streamId.array() );
                       //&m_opinionController.opinionTrafficTx().value().array() );

                // save reference into 'torrentHandleMap'
                m_drive.m_torrentHandleMap[chunkInfoHash] = UseTorrentInfo{ *m_downloadingLtHandle, false };
            }
        });
    }

    void modificationCompletedSuccessfully() override
    {
        DBG_MAIN_THREAD
        
        //TODO ?? (is it all?)

        ModifyDriveTask::modificationCompletedSuccessfully();
    }

    uint64_t getToBeApprovedDownloadSize() override
    {
        return m_request->m_maxSizeBytes;
    }

#ifdef __APPLE__
#pragma mark --acceptFinishStreamTx--
#endif
    
    void acceptFinishStreamTx( mobj<StreamFinishRequest>&& finishStream, std::map<std::array<uint8_t, 32>, ApprovalTransactionInfo>&& opinions ) override
    {
        DBG_MAIN_THREAD
        
        if ( m_finishDataInfoHash )
        {
            _LOG_WARN( "duplicated finish-stream message" )
            return;
        }
        
        if ( finishStream->m_streamId != m_request->m_streamId.array() )
        {
            _LOG_WARN( "ignore unkown stream" )
            return;
        }
        
        m_finishDataInfoHash = finishStream->m_finishDataInfoHash;

        ModifyDriveTask::m_request = std::make_unique<ModificationRequest>( ModificationRequest{ *m_finishDataInfoHash,
                                                                                                 m_request->m_streamId,
                                                                                                 m_request->m_maxSizeBytes,
                                                                                                 m_request->m_replicatorList });
        
        ModifyTaskBase::m_receivedOpinions = std::move(opinions);

        if ( ! m_downloadingLtHandle )
        {
            ModifyDriveTask::startModification();
        }
    }
};

std::unique_ptr<DriveTaskBase> createStreamTask( mobj<StreamRequest>&&       request,
                                                 DriveParams&                drive,
                                                 ModifyOpinionController&    opinionTaskController )
{
    return std::make_unique<StreamTask>( std::move(request), drive, opinionTaskController );
}

} // namespace sirius::drive



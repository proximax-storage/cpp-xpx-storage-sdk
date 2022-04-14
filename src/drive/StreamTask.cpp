/*
*** Copyright 2022 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "drive/Streaming.h"
#include "DriveTaskBase.h"
#include "drive/FsTree.h"
#include "drive/FlatDrive.h"
#include "DriveParams.h"
#include "UpdateDriveTaskBase.h"

namespace sirius::drive
{

namespace fs = std::filesystem;

class StreamTask : public UpdateDriveTaskBase
{
    std::unique_ptr<StreamRequest>  m_request;
    
    // 'ChunkInfo' are sent by 'StreamerClient' via DHT message
    // They are saved in 'm_chunkInfoList' (ordered by m_chunkIndex)
    //
    // Chunks are downloaded one by one
    // 'm_downloadingChunkInfo' is a refference to current downloading (or last downloaded) 'ChunkInfo'
    //
    // If some 'ChunkInfo' message is lost, it will be requested after the lost is detected
    //
    using ChunkInfoList = std::list< std::unique_ptr<ChunkInfo> >;
    ChunkInfoList                   m_chunkInfoList;
    ChunkInfoList::iterator         m_downloadingChunkInfo = m_chunkInfoList.end();
    
    boost::asio::ip::udp::endpoint  m_streamerEndpoint;

public:
    
    StreamTask(  mobj<StreamRequest>&&       request,
                    DriveParams&                drive,
                    ModifyOpinionController&    opinionTaskController )
            :
              UpdateDriveTaskBase( DriveTaskType::STREAM_REQUEST, drive, opinionTaskController )
            , m_request( std::move(request) )
    {
        _ASSERT( m_request )
    }
    
    ~StreamTask()
    {
        _LOG( "m_chunkInfoList.size: " << m_chunkInfoList.size() )
    }
    
    const Hash256& getModificationTransactionHash() override
    {
        return m_request->m_streamId;
    }
    
    virtual void acceptChunkInfoMessage( mobj<ChunkInfo>&& chunkInfo, const boost::asio::ip::udp::endpoint& sender ) override
    {
        if ( chunkInfo->m_streamId != m_request->m_streamId )
        {
            _LOG_WARN( "ignore unkown stream" )
            return;
        }
        
        if ( ! chunkInfo->Verify( m_request->m_streamerKey ) )
        {
            _LOG_WARN( "Bad sign" )
            return;
        }
        
        if ( chunkInfo->m_chunkIndex == 2 )
        {
            _LOG( "chunkInfo->m_chunkIndex == 2" )
        }
        
        m_streamerEndpoint = sender;

        //
        // Save playlist and try to download chunk
        //
        if ( m_chunkInfoList.empty() )
        {
            m_chunkInfoList.emplace_back( std::move(chunkInfo) );
            tryRequestNextChunk();
        }
        else
        {
            auto newChunkIndex = chunkInfo->m_chunkIndex;
            auto backChunkIndex = m_chunkInfoList.back()->m_chunkIndex;
            
            if ( backChunkIndex < newChunkIndex )
            {
                // append chunkInfo
                //
                m_chunkInfoList.emplace_back( std::move(chunkInfo) );
                if ( backChunkIndex+1 == newChunkIndex )
                {
                    tryRequestNextChunk();
                }
                else
                {
                    // request lost chunkInfo
                    for( auto i = backChunkIndex+1; i<newChunkIndex; i++ )
                    {
                        requestMissingChunkInfo( i, sender );
                    }
                }
            }
            else
            {
                // insert chunkInfo in the middle of the list
                //
                auto newChunkIndex = chunkInfo->m_chunkIndex;

                for( auto it = m_chunkInfoList.rbegin(); it != m_chunkInfoList.rend(); it++ )
                {
                    if ( it->get()->m_chunkIndex <= newChunkIndex )
                    {
                        if ( it->get()->m_chunkIndex == newChunkIndex )
                        {
                            // ignore already existing chunkInfo
                        }
                        else
                        {
                            // do insert
                            m_chunkInfoList.insert( it.base(), std::move(chunkInfo) );
                            tryRequestNextChunk();
                        }
                    }
                }
            }
        }
    }
    
    void requestMissingChunkInfo( uint32_t chunkIndex, const boost::asio::ip::udp::endpoint& sender )
    {
        _LOG( "get-chunk-info: " << chunkIndex )
        if ( auto session = m_drive.m_session.lock(); session )
        {
            std::ostringstream os( std::ios::binary );
            cereal::PortableBinaryOutputArchive archive( os );
            archive( chunkIndex );

            session->sendMessage( "get-chunk-info", sender, os.str() );
        }
    }
    
    void tryRequestNextChunk()
    {
        if ( m_downloadingLtHandle )
        {
            // wait the end of downloading
            return;
        }
        
        // assign 'm_downloadingChunkInfo'
        //
        if ( m_downloadingChunkInfo == m_chunkInfoList.end() )
        {
            // set 1-st m_downloadingChunkInfo
            _ASSERT( ! m_chunkInfoList.empty() )
            m_downloadingChunkInfo = m_chunkInfoList.begin();
        }
        else
        {
            auto next = std::next( m_downloadingChunkInfo, 1 );
            if ( next == m_chunkInfoList.end() ||
                 (m_downloadingChunkInfo->get()->m_chunkIndex+1 != next->get()->m_chunkIndex) )
            {
                // we have lost ChunkInfo
                requestMissingChunkInfo( m_downloadingChunkInfo->get()->m_chunkIndex+1, m_streamerEndpoint );
                return;
            }
            m_downloadingChunkInfo = next;
        }

        // start downloading chunk
        //
        if ( auto session = m_drive.m_session.lock(); session )
        {
            const auto& chunkInfoHash = m_downloadingChunkInfo->get()->m_chunkInfoHash;
            
            if ( auto it = m_drive.m_torrentHandleMap.find( chunkInfoHash ); it != m_drive.m_torrentHandleMap.end())
            {
                _ASSERT( it->second.m_ltHandle.is_valid() )
                tryRequestNextChunk();
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

                               _ASSERT( !m_taskIsStopped );

                               if ( code == download_status::failed )
                               {
                                   //todo is it possible?
                                   _ASSERT( 0 );
                                   m_drive.m_torrentHandleMap.erase( infoHash );
                                   tryRequestNextChunk();
                                   return;
                               }

                               if ( code == download_status::download_complete )
                               {
                                   m_downloadingLtHandle.reset();
                                   tryRequestNextChunk();
                               }
                           },
                           chunkInfoHash,
                           *m_opinionController.opinionTrafficTx(),
                           0, true, ""
                   ),
                   m_drive.m_sandboxRootPath,
                   m_drive.m_sandboxRootPath / toString(chunkInfoHash),
                   getUploaders(),
                   &m_drive.m_driveKey.array(),
                   nullptr,
                   &m_opinionController.opinionTrafficTx().value().array() );

            // save reference into 'torrentHandleMap'
            m_drive.m_torrentHandleMap[chunkInfoHash] = UseTorrentInfo{ *m_downloadingLtHandle, false };
        }
    }

    void continueSynchronizingDriveWithSandbox() override
    {
        //TODO
    }

    uint64_t getToBeApprovedDownloadSize() override
    {
        //TODO
        return 0;
    }

    void myOpinionIsCreated() override
    {
        //TODO
    }

    // Whether the finishTask can be called by the task itself
    void tryBreakTask() override
    {
        //TODO
    }

    void run() override
    {
        //TODO
    }

    void terminate() override
    {
        //TODO
    }

};


std::unique_ptr<DriveTaskBase> createStreamTask( mobj<StreamRequest>&&       request,
                                                 DriveParams&                drive,
                                                 ModifyOpinionController&    opinionTaskController )
{
    return std::make_unique<StreamTask>( std::move(request), drive, opinionTaskController );
}

} // namespace sirius::drive



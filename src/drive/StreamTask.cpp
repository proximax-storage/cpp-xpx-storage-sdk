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
    
    boost::asio::ip::udp::endpoint  m_streamerEndpoint;

    ///---

    std::optional<InfoHash>         m_finishInfoHash;
    std::optional<lt_handle>        m_finishLtHandle = {};
    
    using FinishInfo = std::vector<FinishStreamChunkInfo>;
    FinishInfo                      m_finishInfo;
    
    using FinishStreamIt = std::vector<FinishStreamChunkInfo>::iterator;
    FinishStreamIt                  m_finishStreamIt;
    
    ///---

    std::map<std::array<uint8_t,32>,ApprovalTransactionInfo> m_receivedOpinions;

    bool m_modifyApproveTransactionSent = false;
    bool m_modifyApproveTxReceived = false;
    
    uint64_t m_uploadedDataSize = 0;

    std::optional<boost::asio::high_resolution_timer> m_shareMyOpinionTimer;
#ifndef __APPLE__
    const int m_shareMyOpinionTimerDelayMs = 1000 * 60;
#else
    //(???+++)
    const int m_shareMyOpinionTimerDelayMs = 1000 * 1;
#endif

    std::optional<boost::asio::high_resolution_timer> m_modifyOpinionTimer;

    
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
    
    void terminate() override
    {
        DBG_MAIN_THREAD

        //TODO
//        m_modifyOpinionTimer.reset();
//        m_shareMyOpinionTimer.reset();

        breakTorrentDownloadAndRunNextTask();
    }
    
    void tryBreakTask() override
    {
        if ( m_sandboxCalculated & !m_modifyApproveTxReceived )
        {
            finishTask();
        }
        else
        {
            // We will wait the end of current task, that will call m_drive.runNextTask()
        }
    }

    void run() override
    {
        _ASSERT( ! m_opinionController.opinionTrafficTx() );

        m_opinionController.setOpinionTrafficTx( m_request->m_streamId.array() );
    }

    const Hash256& getModificationTransactionHash() override
    {
        return m_request->m_streamId;
    }
    
    virtual std::string acceptGetChunksInfoMessage( uint32_t                               requestedIndex,
                                                    const boost::asio::ip::udp::endpoint&  viewer ) override
    {
        DBG_MAIN_THREAD

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
        
        _ASSERT( chunkInfo->m_chunkIndex == requestedIndex )
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

    virtual void acceptChunkInfoMessage( mobj<ChunkInfo>&& chunkInfo, const boost::asio::ip::udp::endpoint& streamer ) override
    {
        DBG_MAIN_THREAD
        
        _ASSERT( chunkInfo )
        
        std::lock_guard<std::mutex> lock(m_mutex);

        if ( chunkInfo->m_streamId != m_request->m_streamId.array() )
        {
            _LOG_WARN( "ignore unkown stream" )
            return;
        }
        
        if ( ! chunkInfo->Verify( m_request->m_streamerKey ) )
        {
            _LOG_WARN( "Bad sign" )
            return;
        }
        
        m_streamerEndpoint = streamer;

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
    
    void requestMissingChunkInfo( uint32_t chunkIndex, const boost::asio::ip::udp::endpoint& sender )
    {
        DBG_MAIN_THREAD

        _LOG( "get-chunk-info: " << chunkIndex )
        
        if ( auto session = m_drive.m_session.lock(); session )
        {
            std::ostringstream os( std::ios::binary );
            cereal::PortableBinaryOutputArchive archive( os );
            archive( chunkIndex );

            session->sendMessage( "get-chunk-info", sender, os.str() );
        }
    }
    
    void tryDownloadNextChunk()
    {
        DBG_MAIN_THREAD
        
        if ( m_downloadingLtHandle )
        {
            // wait the end of current downloading
            return;
        }

        // break thread, for correct message end-processing
        m_drive.executeOnSessionThread( [this]
        {
            if ( m_downloadingLtHandle )
            {
                // wait the end of current downloading
                return;
            }

            _ASSERT( ! m_downloadingLtHandle )
            
            // select next 'm_downloadingChunkInfoIt'
            //
            if ( ! m_downloadingChunkInfoItWasSet )
            {
                // set 1-st m_downloadingChunkInfoIt
                _ASSERT( ! m_chunkInfoList.empty() )

                if ( m_chunkInfoList.begin()->get() == nullptr )
                {
                    // 1-st ChunkInfo is not received
                    requestMissingChunkInfo( 0, m_streamerEndpoint );
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
                    return;
                }

                if ( next->get() == nullptr )
                {
                    // send request to streamer about missing info
                    requestMissingChunkInfo( m_downloadingChunkInfoIt->get()->m_chunkIndex+1, m_streamerEndpoint );
                    return;
                }

                m_downloadingChunkInfoIt = next;
            }

            // start downloading chunk
            //
            if ( auto session = m_drive.m_session.lock(); session )
            {
                const auto& chunkInfoHash = m_downloadingChunkInfoIt->get()->m_chunkInfoHash;
                
                if ( auto it = m_drive.m_torrentHandleMap.find( chunkInfoHash ); it != m_drive.m_torrentHandleMap.end())
                {
                    _ASSERT( it->second.m_ltHandle.is_valid() )
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

                                   _ASSERT( !m_taskIsStopped );

                                   if ( code == download_status::dn_failed )
                                   {
                                       //todo is it possible?
                                       _ASSERT( 0 );
                                       m_drive.m_torrentHandleMap.erase( infoHash );
                                       m_downloadingLtHandle.reset();
                                       tryDownloadNextChunk();
                                       return;
                                   }

                                   if ( code == download_status::download_complete )
                                   {
                                       m_downloadingLtHandle.reset();
                                       tryDownloadNextChunk();
                                       if ( m_finishInfoHash )
                                       {
                                           m_drive.executeOnBackgroundThread( [this] {
                                               checkFinishCondition();
                                           });
                                       }
                                   }
                               },
                               chunkInfoHash,
                               *m_opinionController.opinionTrafficTx(),
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

    void continueSynchronizingDriveWithSandbox() override
    {
        DBG_BG_THREAD
        
        _LOG( "StreamTask::continueSynchronizingDriveWithSandbox" )

        try
        {
            // update FsTree file & torrent
            if ( ! fs::exists( m_drive.m_sandboxFsTreeFile ) )
            {
                _LOG_ERR( "not exist 1: " << m_drive.m_sandboxFsTreeFile )
            }
            if ( ! fs::exists( m_drive.m_fsTreeFile.parent_path() ) )
            {
                _LOG_ERR( "not exist 2: " <<m_drive.m_fsTreeFile.parent_path() )
            }
            fs::rename( m_drive.m_sandboxFsTreeFile, m_drive.m_fsTreeFile );
            fs::rename( m_drive.m_sandboxFsTreeTorrent, m_drive.m_fsTreeTorrent );

            auto& torrentHandleMap = m_drive.m_torrentHandleMap;
            // remove unused files and torrent files from the drive
            for ( const auto& it : torrentHandleMap )
            {
                const UseTorrentInfo& info = it.second;
                if ( !info.m_isUsed )
                {
                    const auto& hash = it.first;
                    std::string filename = hashToFileName( hash );
                    fs::remove( fs::path( m_drive.m_driveFolder ) / filename );
                    fs::remove( fs::path( m_drive.m_torrentFolder ) / filename );
                }
            }

            // remove unused data from 'fileMap'
            std::erase_if( torrentHandleMap, []( const auto& it )
            { return !it.second.m_isUsed; } );

            //
            // Add torrents into session
            //
            for ( auto& it : torrentHandleMap )
            {
                // load torrent (if it is not loaded)
                //(???+++) unused code
                if ( ! it.second.m_ltHandle.is_valid())
                {
                    if ( auto session = m_drive.m_session.lock(); session )
                    {
                        std::string fileName = hashToFileName( it.first );
                        it.second.m_ltHandle = session->addTorrentFileToSession(
                                m_drive.m_torrentFolder / fileName,
                                m_drive.m_driveFolder,
                                lt::SiriusFlags::peer_is_replicator,
                                &m_drive.m_driveKey.array(),
                                nullptr,
                                nullptr );
                        _ASSERT( it.second.m_ltHandle.is_valid() )
                        _LOG( "downloading: ADDED_TO_SESSION : " << m_drive.m_torrentFolder / fileName )
                    }
                }
            }

            // Add FsTree torrent to session
            if ( auto session = m_drive.m_session.lock(); session )
            {
                m_sandboxFsTreeLtHandle = session->addTorrentFileToSession( m_drive.m_fsTreeTorrent,
                                                                            m_drive.m_fsTreeTorrent.parent_path(),
                                                                            lt::SiriusFlags::peer_is_replicator,
                                                                            &m_drive.m_driveKey.array(),
                                                                            nullptr,
                                                                            nullptr );
            }

            m_drive.executeOnSessionThread( [this]() mutable
                                            {
                                                synchronizationIsCompleted();
                                            } );
        }
        catch (const std::exception& ex)
        {
            _LOG_WARN( "exception during continueSynchronizingDriveWithSandbox: " << ex.what());
            finishTask();
        }
    }

    void modifyIsCompleted() override
    {
        DBG_MAIN_THREAD
        
        _LOG( "modifyIsCompleted" );

        if ( m_drive.m_dbgEventHandler ) {
            m_drive.m_dbgEventHandler->driveModificationIsCompleted(
                    m_drive.m_replicator, m_drive.m_driveKey, m_request->m_streamId, *m_sandboxRootHash);
        }

        UpdateDriveTaskBase::modifyIsCompleted();
    }

    uint64_t getToBeApprovedDownloadSize() override
    {
        return m_request->m_maxSizeBytes;
    }

    void shareMyOpinion()
    {
        DBG_MAIN_THREAD

        std::ostringstream os( std::ios::binary );
        cereal::PortableBinaryOutputArchive archive( os );
        archive( *m_myOpinion );

        for ( const auto& replicatorIt : m_drive.getAllReplicators())
        {
            m_drive.m_replicator.sendMessage( "opinion", replicatorIt.array(), os.str());
        }
    }

    void myOpinionIsCreated() override
    {
        DBG_MAIN_THREAD

        _ASSERT( m_myOpinion )

        if ( m_taskIsStopped )
        {
            finishTask();
            return;
        }

        m_sandboxCalculated = true;

        if ( m_modifyApproveTxReceived )
        {
            sendSingleApprovalTransaction( *m_myOpinion );
            startSynchronizingDriveWithSandbox();
        }
        else
        {
            // Send my opinion to other replicators
            shareMyOpinion();
            if ( auto session = m_drive.m_session.lock(); session )
            {
                m_shareMyOpinionTimer = session->startTimer( m_shareMyOpinionTimerDelayMs, [this]
                {
                    _LOG( "shareMyOpinion" )
                    shareMyOpinion();
                } );
            }

            // validate already received opinions
            std::erase_if( m_receivedOpinions, [this]( const auto& item )
            {
                return !validateOpinion( item.second );
            } );

            // Maybe send approval transaction
            checkOpinionNumberAndStartTimer();
        }
    }
    
    bool validateOpinion( const ApprovalTransactionInfo& anOpinion )
    {
        bool equal = m_myOpinion->m_rootHash == anOpinion.m_rootHash &&
                     m_myOpinion->m_fsTreeFileSize == anOpinion.m_fsTreeFileSize &&
                     m_myOpinion->m_metaFilesSize == anOpinion.m_metaFilesSize &&
                     m_myOpinion->m_driveSize == anOpinion.m_driveSize;
        return equal;
    }

    void checkOpinionNumberAndStartTimer()
    {
        DBG_MAIN_THREAD

        // m_drive.getReplicator()List is the list of other replicators (it does not contain our replicator)
#ifndef MINI_SIGNATURE
        auto replicatorNumber = m_drive.getAllReplicators().size() + 1;
#else
        auto replicatorNumber = m_drive.getAllReplicators().size();//todo++++ +1;
#endif

// check opinion number
        if ( m_myOpinion &&
                m_receivedOpinions.size() >=
             ((replicatorNumber) * 2) / 3 &&
             !m_modifyApproveTransactionSent &&
             !m_modifyApproveTxReceived )
        {
            // start timer if it is not started
            if ( !m_modifyOpinionTimer )
            {
                if ( auto session = m_drive.m_session.lock(); session )
                {
                    m_modifyOpinionTimer = session->startTimer(
                            m_drive.m_replicator.getModifyApprovalTransactionTimerDelay(),
                            [this]()
                            { opinionTimerExpired(); } );
                }
            }
        }
    }

    void opinionTimerExpired()
    {
        DBG_MAIN_THREAD

        if ( m_modifyApproveTransactionSent || m_modifyApproveTxReceived )
            return;

        ApprovalTransactionInfo info = {m_drive.m_driveKey.array(),
                                        m_myOpinion->m_modifyTransactionHash,
                                        m_myOpinion->m_rootHash,
                                        m_myOpinion->m_fsTreeFileSize,
                                        m_myOpinion->m_metaFilesSize,
                                        m_myOpinion->m_driveSize,
                                        {}};

        info.m_opinions.reserve( m_receivedOpinions.size() + 1 );
        info.m_opinions.emplace_back( m_myOpinion->m_opinions[0] );
        for ( const auto& otherOpinion : m_receivedOpinions )
        {
            info.m_opinions.emplace_back( otherOpinion.second.m_opinions[0] );
        }

        // notify event handler
        m_drive.m_eventHandler.modifyApprovalTransactionIsReady( m_drive.m_replicator, info );

        m_modifyApproveTransactionSent = true;
    }
    
    bool processedModifyOpinion( const ApprovalTransactionInfo& anOpinion ) override
    {
        _LOG( "processedModifyOpinion" )
        
        // In this case Replicator is able to verify all data in the opinion
        if ( m_myOpinion &&
             m_request->m_streamId.array() == anOpinion.m_modifyTransactionHash &&
             validateOpinion( anOpinion ) )
        {
            m_receivedOpinions[anOpinion.m_opinions[0].m_replicatorKey] = anOpinion;
            checkOpinionNumberAndStartTimer();
        }
        return true;
    }
    
    bool onApprovalTxPublished( const PublishedModificationApprovalTransactionInfo& transaction ) override
    {
        DBG_MAIN_THREAD
        
        if ( m_taskIsStopped )
        {
            return true;
        }

        m_modifyApproveTxReceived = true;

        if ( m_request->m_streamId == transaction.m_modifyTransactionHash )
        {
            if ( ! m_sandboxCalculated )
            {
                return false;
            }

            if ( *m_sandboxRootHash != transaction.m_rootHash ) {
                _LOG_ERR( "Invalid Sandbox Root Hash: " << *m_sandboxRootHash << " " << Hash256(transaction.m_rootHash) )
            }
            
            const auto& v = transaction.m_replicatorKeys;
            auto it = std::find( v.begin(), v.end(), m_drive.m_replicator.replicatorKey().array());

            // Is my opinion present in the transaction ?
            if ( it == v.end() )
            {
                // Send Single Approval Transaction At First
                sendSingleApprovalTransaction( *m_myOpinion );
            }

            startSynchronizingDriveWithSandbox();
            return false;
        }
        else
        {
            m_opinionController.increaseApprovedExpectedCumulativeDownload(m_request->m_maxSizeBytes);
            breakTorrentDownloadAndRunNextTask();
            return true;
        }
    }


#ifdef __APPLE__
#pragma mark --FinishStream--
#endif
    
    void acceptFinishStreamMessage( mobj<FinishStream>&& finishStream, const boost::asio::ip::udp::endpoint& streamer ) override
    {
        DBG_MAIN_THREAD

        if ( m_finishInfoHash )
        {
            _LOG_WARN( "duplicated finish-stream message" )
            return;
        }
            
        if ( finishStream->m_streamId != m_request->m_streamId.array() )
        {
            _LOG_WARN( "ignore unkown stream" )
            return;
        }
        
        if ( ! finishStream->Verify( m_request->m_streamerKey ) )
        {
            _LOG_WARN( "Bad sign" )
            return;
        }
        
        m_finishInfoHash = finishStream->m_finishDataInfoHash;
        
        if ( auto it = m_drive.m_torrentHandleMap.find( *m_finishInfoHash ); it != m_drive.m_torrentHandleMap.end())
        {
            _ASSERT( it->second.m_ltHandle.is_valid() )
            m_finishLtHandle = it->second.m_ltHandle;
            m_drive.executeOnBackgroundThread( [this]
            {
                parseFinishInfoFile();
            });
            return;
        }
        
        // start downloading finish info
        //
        if ( auto session = m_drive.m_session.lock(); session )
        {
            m_finishLtHandle = session->download(
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

                               if ( code == download_status::dn_failed )
                               {
                                   //todo is it possible?
                                   _ASSERT( 0 );
                                   m_drive.m_torrentHandleMap.erase( infoHash );
                                   return;
                               }

                               if ( code == download_status::download_complete )
                               {
                                   m_drive.executeOnBackgroundThread( [this]
                                   {
                                       parseFinishInfoFile();
                                   });
                               }
                           },
                           *m_finishInfoHash,
                           *m_opinionController.opinionTrafficTx(),
                           0, true, ""
                   ),
                   m_drive.m_sandboxStreamFolder,
                   m_drive.m_sandboxStreamTFolder / toString(*m_finishInfoHash),
                   getUploaders(),
                   &m_drive.m_driveKey.array(),
                   nullptr,
                   &m_request->m_streamId.array() );

            // save reference into 'torrentHandleMap'
            m_drive.m_torrentHandleMap[*m_finishInfoHash] = UseTorrentInfo{ *m_finishLtHandle, false };
        }
    }

    void parseFinishInfoFile()
    {
        DBG_BG_THREAD
    
        // parse finish info file
        try
        {
            auto status = m_finishLtHandle->status( lt::torrent_handle::query_save_path | lt::torrent_handle::query_name );
            _LOG( " file:" << status.save_path << "/" << status.name );

            std::ifstream finishInfoFile( fs::path(status.save_path) / status.name, std::ios::binary );
            cereal::PortableBinaryInputArchive iarchive(finishInfoFile);
            iarchive( m_finishInfo );
        }
        catch(...)
        {
            _LOG_WARN( "Error in finishStreamFile" )
            return;
        }
        m_finishStreamIt = m_finishInfo.begin();
        
        continueFinishStream();
    }

    void continueFinishStream()
    {
        DBG_BG_THREAD
        
        if ( m_finishStreamIt == m_finishInfo.end() )
        {
            return;
        }
        
        std::lock_guard<std::mutex> lock(m_mutex);

        for( ; m_finishStreamIt != m_finishInfo.end(); m_finishStreamIt++ )
        {
            if ( m_drive.m_torrentHandleMap.find( m_finishStreamIt->m_chunkInfoHash ) == m_drive.m_torrentHandleMap.end() )
            {
                m_drive.executeOnSessionThread( [this]
                {
                    requestMissingChunkInfo( m_finishStreamIt->m_chunkIndex, m_streamerEndpoint );
                });
                return;
            }
        }
    }


    void checkFinishCondition()
    {
        DBG_BG_THREAD
        
        if ( m_finishInfo.size() == 0 ) // finishInfo-file not received
        {
            return;
        }
        
        if ( m_chunkInfoList.size() < m_finishInfo.size() )
        {
            return;
        }
        
        if ( m_chunkInfoList.size() > m_finishInfo.size() )
        {
            //TODO cancel tx
            _LOG( "m_chunkInfoList.size() > m_finishInfo.size(): " << m_chunkInfoList.size() << " " << m_finishInfo.size() )
            return;
        }
        
        // check that all chunk-INFO are received
        //
        bool allChunkInfoReceived = true;
        for( size_t i=0; i<m_chunkInfoList.size(); i++ )
        {
            const auto& chunkInfo = m_chunkInfoList[i];
            if ( chunkInfo.get() == nullptr )
            {
                requestMissingChunkInfo( i, m_streamerEndpoint );
                allChunkInfoReceived = false;
            }
        }
        
        if ( ! allChunkInfoReceived )
        {
            return;
        }
        
        // check that all chunks are received
        //
        for( size_t i=0; i<m_chunkInfoList.size(); i++ )
        {
            const auto& chunkInfo = m_chunkInfoList[i];
            if ( m_drive.m_torrentHandleMap.find( chunkInfo->m_chunkInfoHash ) == m_drive.m_torrentHandleMap.end() )
            {
                // chunk is not received
                _LOG( "chunk is not received: " << chunkInfo->m_chunkIndex );
                return;
            }

            if ( chunkInfo->m_chunkInfoHash != m_finishInfo[i].m_chunkInfoHash )
            {
                // bad finish-info
                _LOG( "bad finish-info: " << chunkInfo->m_chunkIndex );
                return;
            }
        }

        completeStreamFinishing();
    }

    void completeStreamFinishing()
    {
        m_sandboxFsTree->deserialize( m_drive.m_fsTreeFile );
        
        if ( ! m_sandboxFsTree->addFolder( m_request->m_folder ) )
        {
            _LOG_WARN( "cannot add folder: " << m_request->m_folder )
            //todo cancel tx
            return;
        }
        
        Folder* streamFolder = m_sandboxFsTree->getFolderPtr( m_request->m_folder );

        for( const auto& chunkInfo : m_finishInfo )
        {
            if ( chunkInfo.m_saveOnDrive )
            {
                streamFolder->m_childs.emplace_front( File{ toString(chunkInfo.m_chunkInfoHash), chunkInfo.m_chunkInfoHash, chunkInfo.m_sizeBytes} );
            }
        }
        
        //
        // Generate playlist
        //
        
        std::stringstream playlist;
        playlist << "#EXTM3U" << std::endl;
        playlist << "#EXT-X-VERSION:3" << std::endl;

        uint32_t maxDuration = 1;
        for( const auto& chunkInfo : m_finishInfo )
        {
            uint32_t seconds = (chunkInfo.m_durationMks+100000-1)/1000000;
            if ( maxDuration < seconds )
            {
                maxDuration = seconds;
            }
        }

        playlist << "#EXT-X-TARGETDURATION:" << maxDuration << std::endl;
        playlist << "#EXT-X-MEDIA-SEQUENCE:" << 0 << std::endl;
        
        for( const auto& chunkInfo : m_finishInfo )
        {
            playlist << "#EXTINF:" << chunkInfo.m_durationMks/1000000 << "." << chunkInfo.m_durationMks%1000000 << std::endl;
            playlist << Key(chunkInfo.m_chunkInfoHash) << std::endl;
        }

        auto playlistTxt = playlist.str();

        fs::path tmp = m_drive.m_sandboxRootPath / PLAYLIST_FILE_NAME;
        {
            std::ofstream fileStream( tmp, std::ios::binary );
            fileStream.write( playlistTxt.c_str(), playlistTxt.size() );
        }
        
        // Calculate infoHash of playlist
        InfoHash finishPlaylistHash = createTorrentFile( tmp, m_drive.m_driveKey, m_drive.m_sandboxRootPath, {} );
        fs::path finishPlaylistFilename = m_drive.m_driveFolder / toString( finishPlaylistHash );
        fs::path torrentFilename = m_drive.m_torrentFolder / toString( finishPlaylistHash );
        if ( ! fs::exists(finishPlaylistFilename) )
        {
            fs::rename( tmp, finishPlaylistFilename );
        }
        if ( ! fs::exists(torrentFilename) )
        {
            InfoHash finishPlaylistHash2 = createTorrentFile( finishPlaylistFilename,
                                                              m_drive.m_driveKey,
                                                              m_drive.m_driveFolder,
                                                              torrentFilename );
            _ASSERT( finishPlaylistHash2 == finishPlaylistHash )
        }
        
        streamFolder->m_childs.emplace_front( File{ PLAYLIST_FILE_NAME, finishPlaylistHash, playlistTxt.size() } );

        m_sandboxFsTree->doSerialize( m_drive.m_sandboxFsTreeFile );

        m_sandboxRootHash = createTorrentFile( m_drive.m_sandboxFsTreeFile,
                                               m_drive.m_driveKey,
                                               m_drive.m_sandboxRootPath,
                                               m_drive.m_sandboxFsTreeTorrent );

        getSandboxDriveSizes( m_metaFilesSize, m_sandboxDriveSize );
        m_fsTreeSize = sandboxFsTreeSize();
        
        if ( m_metaFilesSize + m_sandboxDriveSize + m_fsTreeSize > m_drive.m_maxSize )
        {
//            m_drive.executeOnSessionThread( [this] {
//                modifyIsCompletedWithError( "Drive is full", 0 );
//            });
            return;
        }

        m_drive.executeOnSessionThread( [this,torrentFilename]() //mutable
        {
            if ( auto session = m_drive.m_session.lock(); session )
            {
                session->addTorrentFileToSession( torrentFilename,
                                                  m_drive.m_driveFolder,
                                                  lt::SiriusFlags::peer_is_replicator,
                                                  &m_drive.m_driveKey.array(),
                                                  nullptr,
                                                  &m_request->m_streamId.array(),
                                                  {},
                                                  nullptr );
            }
            myRootHashIsCalculated();
        } );

    }
    
};

std::unique_ptr<DriveTaskBase> createStreamTask( mobj<StreamRequest>&&       request,
                                                 DriveParams&                drive,
                                                 ModifyOpinionController&    opinionTaskController )
{
    return std::make_unique<StreamTask>( std::move(request), drive, opinionTaskController );
}

} // namespace sirius::drive



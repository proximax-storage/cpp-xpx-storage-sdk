/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "DownloadLimiter.h"
#include "DriveTaskBase.h"
#include "drive/FsTree.h"
#include "drive/ActionList.h"
#include "drive/FlatDrive.h"
#include "DriveParams.h"
#include "UpdateDriveTaskBase.h"

#include <boost/multiprecision/cpp_int.hpp>

#include <numeric>

#include <cereal/types/vector.hpp>
#include <cereal/types/array.hpp>
#include <cereal/types/map.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/archives/portable_binary.hpp>

namespace sirius::drive
{

namespace fs = std::filesystem;

class ModifyDriveTask : public UpdateDriveTaskBase
{

private:

    const mobj<ModificationRequest> m_request;

    std::set<InfoHash> m_missedFileSet;

    std::map<std::array<uint8_t,32>,ApprovalTransactionInfo> m_receivedOpinions;

    bool m_actionListIsReceived = false;
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

    ModifyDriveTask(
            mobj<ModificationRequest>&& request,
            std::map<std::array<uint8_t,32>,ApprovalTransactionInfo>&& receivedOpinions,
            DriveParams& drive,
            ModifyOpinionController& opinionTaskController)
            : UpdateDriveTaskBase( DriveTaskType::MODIFICATION_REQUEST, drive, opinionTaskController )
            , m_request( std::move(request) )
            , m_receivedOpinions( std::move( receivedOpinions ))
    {
        _ASSERT( m_request )
    }

    void terminate() override
    {
        DBG_MAIN_THREAD

        m_modifyOpinionTimer.reset();
        m_shareMyOpinionTimer.reset();

        breakTorrentDownloadAndRunNextTask();
    }

    void run() override
    {
        DBG_MAIN_THREAD
        
        m_uploadedDataSize = 0;
        
        _ASSERT( !m_opinionController.opinionTrafficTx() );

        m_opinionController.setOpinionTrafficTx( m_request->m_transactionHash.array() );

        //_LOG( "?????????: " << m_request->m_clientDataInfoHash  << "   " << m_drive.m_torrentHandleMap.size() )
        if ( auto it = m_drive.m_torrentHandleMap.find( m_request->m_clientDataInfoHash ); it != m_drive.m_torrentHandleMap.end() )
        {
            m_actionListIsReceived = true;

            m_drive.executeOnBackgroundThread( [this]
            {
                 prepareDownloadMissingFiles();
            } );
        }
        
        if ( auto session = m_drive.m_session.lock(); session )
        {
            m_downloadingLtHandle = session->download(
                                        DownloadContext(
                                               DownloadContext::client_data,
                                                       
                                                   [this]( download_status::code        code,
                                                           const InfoHash&              infoHash,
                                                           const std::filesystem::path  saveAs,
                                                           size_t                       downloadedSize,
                                                           size_t                       /*fileSize*/,
                                                           const std::string&           errorText )
                                                   {
                                                       //(???+)
                                                       DBG_MAIN_THREAD

                                                       if ( code == download_status::dn_failed )
                                                       {
                                                           m_drive.m_torrentHandleMap.erase( infoHash );
                                                           modifyIsCompletedWithError( errorText, 0 );
                                                       }
                                                       else if ( code == download_status::download_complete )
                                                       {
                                                           //(???+++)
                                                           _ASSERT( !m_taskIsStopped );
                                                           if ( ! m_taskIsStopped )
                                                           {
                                                               m_uploadedDataSize += downloadedSize;
                                                               m_actionListIsReceived = true;

                                                               m_drive.executeOnBackgroundThread( [this]
                                                               {
                                                                    prepareDownloadMissingFiles();
                                                               } );
                                                           }
                                                       }
                                                   },
                                                   m_request->m_clientDataInfoHash,
                                                   m_request->m_transactionHash,
                                                   m_request->m_maxDataSize - m_uploadedDataSize,
                                                   true,
                                                   "" ),
                                               m_drive.m_sandboxRootPath,
                                               "",
                                               getUploaders(),
                                               &m_drive.m_driveKey.array(),
                                               nullptr,
                                               &m_request->m_transactionHash.array()
                                                );
        }
    }

    void prepareDownloadMissingFiles()
    {
        DBG_BG_THREAD

        std::error_code err;

        auto actionListFilename = m_drive.m_sandboxRootPath / hashToFileName( m_request->m_clientDataInfoHash );

        // Check 'ActionList' is received
        if ( !fs::exists( actionListFilename, err ))
        {
            _LOG_WARN( "modifyDriveInSandbox: 'ActionList.bin' is absent: "
                              << m_drive.m_clientActionListFile );
            m_drive.executeOnSessionThread( [this] { modifyIsCompletedWithError( "modify drive: 'ActionList' is absent", -1 ); } );
            return;
        }

        // Load 'actionList' into memory
        ActionList actionList;
        try {
            actionList.deserialize( actionListFilename );
        } catch (...)
        {
            _LOG_WARN( "modifyDriveInSandbox: invalid 'ActionList'" << m_request->m_clientDataInfoHash );
            m_drive.executeOnSessionThread( [this] { modifyIsCompletedWithError( "modify drive: invalid 'ActionList'", -1 ); } );
        }
        
        // prepare 'm_missedFileSet'
        for ( const Action& action : actionList )
        {
            if ( action.m_isInvalid )
                continue;

            switch (action.m_actionId)
            {
                case action_list_id::upload:
                    m_missedFileSet.insert( stringToHash( action.m_param1 ) );
                    break;
                case action_list_id::new_folder:
                case action_list_id::move:
                case action_list_id::remove:
                    break;
            } // end of switch()
        } // end of for( const Action& action : actionList )

        m_drive.executeOnSessionThread( [this]
        {
            downloadMissingFiles();
        });
    }

    void downloadMissingFiles()
    {
        DBG_MAIN_THREAD

        _ASSERT( !m_taskIsStopped );

        std::optional<Hash256> fileToDownload;

        while ( !m_missedFileSet.empty() && !fileToDownload )
        {
            fileToDownload = *m_missedFileSet.begin();
            m_missedFileSet.erase( m_missedFileSet.begin());

            if ( auto it = m_drive.m_torrentHandleMap.find( *fileToDownload ); it != m_drive.m_torrentHandleMap.end())
            {
                _ASSERT( it->second.m_ltHandle.is_valid() )
                fileToDownload.reset();
            }
        }

        if ( fileToDownload )
        {
            if ( auto session = m_drive.m_session.lock(); session )
            {
                _ASSERT( m_opinionController.opinionTrafficTx())
                _LOG( "+++ ex downloading: START: " << toString( *fileToDownload ));
                m_downloadingLtHandle = session->download( DownloadContext(

                                                                   DownloadContext::missing_files,

                                                                   [this]( download_status::code code,
                                                                           const InfoHash& infoHash,
                                                                           const std::filesystem::path saveAs,
                                                                           size_t downloadedSize,
                                                                           size_t /*fileSize*/,
                                                                           const std::string& errorText )
                                                                   {
                                                                       DBG_MAIN_THREAD

                                                                       if ( code == download_status::download_complete )
                                                                       {
                                                                           _LOG( "downloading: END: " << toString( infoHash ));
                                                                           m_uploadedDataSize += downloadedSize;
                                                                           downloadMissingFiles();
                                                                       } else if ( code == download_status::dn_failed )
                                                                       {
                                                                           m_drive.m_torrentHandleMap.erase( infoHash );
                                                                           modifyIsCompletedWithError( errorText, 0 );
                                                                       }
                                                                   },

                                                                   *fileToDownload,
                                                                   m_request->m_transactionHash,
                                                                   m_request->m_maxDataSize - m_uploadedDataSize,
                                                                   true,
                                                                   "" ),
                                                           m_drive.m_driveFolder,
                                                           m_drive.m_torrentFolder / (toString(*fileToDownload)),
                                                           getUploaders(),
                                                           &m_drive.m_driveKey.array(),
                                                           nullptr,
                                                           &m_request->m_transactionHash.array()
                                                          );
            }

            // save reference into 'torrentHandleMap'
            m_drive.m_torrentHandleMap[*fileToDownload] = UseTorrentInfo{ *m_downloadingLtHandle, false };
        }
        else
        {
            _LOG( "Download Handle Reset" )
            m_downloadingLtHandle.reset();

            // it is the end of list
            m_drive.executeOnBackgroundThread( [this]
                                               {
                                                   modifyFsTreeInSandbox();
                                               } );
        }
    }
    
    void modifyFsTreeInSandbox()
    {
        DBG_BG_THREAD

        // Load 'actionList' into memory
        ActionList actionList;
        auto actionListFilename = m_drive.m_sandboxRootPath / hashToFileName( m_request->m_clientDataInfoHash );
        actionList.deserialize( actionListFilename );

        // Make copy of current FsTree
        m_sandboxFsTree->deserialize( m_drive.m_fsTreeFile );

        auto& torrentHandleMap = m_drive.m_torrentHandleMap;

        //
        // Perform actions
        //
        for ( const Action& action : actionList )
        {
            if ( action.m_isInvalid )
                continue;

            switch (action.m_actionId)
            {
                    //
                    // Upload
                    //
                case action_list_id::upload:
                {
                    std::error_code err;

                    // Check that file exists in client folder
                    fs::path clientFile = m_drive.m_driveFolder / action.m_param1;
                    if ( !fs::exists( clientFile, err ) || fs::is_directory( clientFile, err ))
                    {
                        _LOG( "! ActionList: invalid 'upload': file not exists: " << clientFile )
                        _LOG_WARN( "! ActionList: invalid 'upload': file not exists: " << action.m_param2 )
                        action.m_isInvalid = true;
                        break;
                    }

                    try
                    {
                        size_t fileSize = std::filesystem::file_size( clientFile );
                        auto fileHash = stringToHash(action.m_param1);
                        
                        //
                        // add file in resultFsTree
                        //
                        Folder::Child* destEntry = m_sandboxFsTree->getEntryPtr( action.m_param2 );
                        fs::path destFolder;
                        fs::path srcFile;
                        if ( destEntry != nullptr && isFolder( *destEntry ))
                        {
                            //srcFile = fs::path( action.m_param1 ).filename();
                            srcFile = action.m_filename;
                            destFolder = action.m_param2;
                        } else
                        {
                            srcFile = fs::path( action.m_param2 ).filename();
                            destFolder = fs::path( action.m_param2 ).parent_path();
                        }
                        m_sandboxFsTree->addFile( destFolder,
                                                  srcFile,
                                                  fileHash,
                                                  fileSize );

                        _LOG( "ActionList: successful 'upload': " << clientFile )
                    }
                    catch (const std::exception& error)
                    {
                        _LOG_ERR( "ActionList: exception during 'upload': " << clientFile << "; " << error.what())
                    }
                    catch (...)
                    {
                        _LOG_ERR( "ActionList: unknown exception during 'upload': " << clientFile )
                    }
                    break;
                }
                    //
                    // New folder
                    //
                case action_list_id::new_folder:
                {
                    // Check that entry is free
                    if ( m_sandboxFsTree->getEntryPtr( action.m_param1 ) != nullptr )
                    {
                        _LOG( "! ActionList: invalid 'new_folder': such entry already exists: " << action.m_param1 )
                        action.m_isInvalid = true;
                        break;
                    }

                    m_sandboxFsTree->addFolder( action.m_param1 );

                    _LOG( "ActionList: successful 'new_folder': " << action.m_param1 )
                    break;
                }
                    //
                    // Move
                    //
                case action_list_id::move:
                {
                    auto* srcChild = m_sandboxFsTree->getEntryPtr( action.m_param1 );

                    // Check that src child exists
                    if ( srcChild == nullptr )
                    {
                        _LOG( "! ActionList: invalid 'move': 'srcPath' not exists (in FsTree): " << action.m_param1 )
                        action.m_isInvalid = true;
                        break;
                    }

                    // Check topology (nesting folders)
                    if ( isFolder( *srcChild ))
                    {
                        fs::path srcPath = fs::path( "root" ) / action.m_param1;
                        fs::path destPath = fs::path( "root" ) / action.m_param2;

                        // srcPath should not be a parent folder of destPath
                        if ( isPathInsideFolder( srcPath, destPath ))
                        {
                            _LOG( "! ActionList: invalid 'move': 'srcPath' is a directory which is an ancestor of 'destPath'" )
                            _LOG( "  invalid 'move': 'srcPath' : " << action.m_param1 );
                            _LOG( "  invalid 'move': 'destPath' : " << action.m_param2 );
                            action.m_isInvalid = true;
                            break;
                        }
                    }

                    // modify FsTree
                    m_sandboxFsTree->moveFlat( action.m_param1, action.m_param2,
                                               [/*this*/]( const InfoHash& /*fileHash*/ )
                                               {
                                                   //m_torrentMap.try_emplace( fileHash, UseTorrentInfo{} );
                                               } );

                    _LOG( "ActionList: successful 'move': " << action.m_param1 << " -> " << action.m_param2 )
                    break;
                }
                    //
                    // Remove
                    //
                case action_list_id::remove:
                {
                    if ( m_sandboxFsTree->getEntryPtr( action.m_param1 ) == nullptr )
                    {
                        _LOG( "! ActionList: invalid 'remove': 'srcPath' not exists (in FsTree): "
                                      << action.m_param1 );
                        //m_sandboxFsTree.dbgPrint();
                        action.m_isInvalid = true;
                        break;
                    }

                    // remove entry from FsTree
                    m_sandboxFsTree->removeFlat( action.m_param1, [&]( const InfoHash& fileHash )
                    {
                        // maybe it is file from client data, so we add it to map with empty torrent handle
                        torrentHandleMap.try_emplace( fileHash, UseTorrentInfo{} );
                    } );

                    _LOG( "! ActionList: successful 'remove': " << action.m_param1 );
                    break;
                }

            } // end of switch()
        } // end of for( const Action& action : actionList )

        // create FsTree in sandbox
        m_sandboxFsTree->doSerialize( m_drive.m_sandboxFsTreeFile );

        m_sandboxRootHash = createTorrentFile( m_drive.m_sandboxFsTreeFile,
                                               m_drive.m_driveKey,
                                               m_drive.m_sandboxRootPath,
                                               m_drive.m_sandboxFsTreeTorrent );

        getSandboxDriveSizes( m_metaFilesSize, m_sandboxDriveSize );
        m_fsTreeSize = sandboxFsTreeSize();
        
        if ( m_metaFilesSize + m_sandboxDriveSize + m_fsTreeSize > m_drive.m_maxSize )
        {
            m_drive.executeOnSessionThread( [this] {
                modifyIsCompletedWithError( "Drive is full", 0 );
            });
            return;
        }

        m_drive.executeOnSessionThread( [this]() mutable
                                        {
                                            myRootHashIsCalculated();
                                        } );
    }

    bool processedModifyOpinion( const ApprovalTransactionInfo& anOpinion ) override
    {
        // In this case Replicator is able to verify all data in the opinion
        if ( m_myOpinion &&
             m_request->m_transactionHash.array() == anOpinion.m_modifyTransactionHash &&
             validateOpinion( anOpinion ) )
        {
            m_receivedOpinions[anOpinion.m_opinions[0].m_replicatorKey] = anOpinion;
            checkOpinionNumberAndStartTimer();
        }
        return true;
    }

    // Returns 'true' if 'CatchingUp' should be started

    bool onApprovalTxPublished( const PublishedModificationApprovalTransactionInfo& transaction ) override
    {
        DBG_MAIN_THREAD
        
        if ( m_taskIsStopped )
        {
            return true;
        }

        m_modifyApproveTxReceived = true;

        if ( m_request->m_transactionHash == transaction.m_modifyTransactionHash
             && m_actionListIsReceived )
        {
            if ( !m_sandboxCalculated )
            {
                return false;
            }
            
            _ASSERT( m_sandboxRootHash == transaction.m_rootHash )
            
            const auto& v = transaction.m_replicatorKeys;
            auto it = std::find( v.begin(), v.end(), m_drive.m_replicator.replicatorKey().array());

            // Is my opinion present in the transaction
            if ( it == v.end())
            {
                // Send Single Approval Transaction At First
                sendSingleApprovalTransaction( *m_myOpinion );
            }

            startSynchronizingDriveWithSandbox();
            return false;
        }
        else
        {
            m_opinionController.increaseApprovedExpectedCumulativeDownload(m_request->m_maxDataSize);
            breakTorrentDownloadAndRunNextTask();
            return true;
        }
    }

    bool shouldCancelModify( const ModificationCancelRequest& cancelRequest ) override
    {
        if ( m_taskIsStopped )
        {
            return false;
        }

        if ( cancelRequest.m_modifyTransactionHash == m_request->m_transactionHash )
        {
            breakTorrentDownloadAndRunNextTask();
            return true;
        }

        return false;
    }

    void onApprovalTxFailed( const Hash256& transactionHash ) override
    {
        DBG_MAIN_THREAD

        if ( m_request->m_transactionHash == transactionHash &&
             !m_taskIsStopped &&
             !m_modifyApproveTxReceived )
        {
            m_modifyApproveTransactionSent = false;
            for ( const auto&[key, opinion]: m_receivedOpinions )
            {
                m_drive.m_replicator.processOpinion( opinion );
            }
            m_receivedOpinions.clear();
        }
    }
    
    void tryBreakTask() override
    {
        if ( m_sandboxCalculated )
        {
            // we will wait the end of current task, that will call m_drive.runNextTask()
        }
        else
        {
            finishTask();
        }
    }

protected:

    void modifyIsCompleted() override
    {
        DBG_MAIN_THREAD
        
        _LOG( "modifyIsCompleted" );

        if ( m_drive.m_dbgEventHandler ) {
            m_drive.m_dbgEventHandler->driveModificationIsCompleted(
                    m_drive.m_replicator, m_drive.m_driveKey, m_request->m_transactionHash, *m_sandboxRootHash);
        }

        UpdateDriveTaskBase::modifyIsCompleted();
    }

    void modifyIsCompletedWithError( std::string errorText, int errorCode )
    {
        DBG_MAIN_THREAD
        _LOG( "modifyIsCompletedWithError" );

        if ( m_drive.m_dbgEventHandler )
        {
            m_drive.m_dbgEventHandler->modifyTransactionEndedWithError(
                                         m_drive.m_replicator,
                                         m_drive.m_driveKey,
                                         *m_request,
                                         errorText, errorCode );
        }

        m_sandboxRootHash = m_drive.m_rootHash;
        m_sandboxFsTree->deserialize( m_drive.m_fsTreeFile );
        std::error_code ec;
        fs::remove( m_drive.m_sandboxFsTreeFile, ec );
        fs::copy( m_drive.m_fsTreeFile, m_drive.m_sandboxFsTreeFile );
        fs::remove( m_drive.m_sandboxFsTreeTorrent, ec );
        fs::copy( m_drive.m_fsTreeTorrent, m_drive.m_sandboxFsTreeTorrent );

        m_downloadingLtHandle.reset();
        myRootHashIsCalculated();
    }

    const Hash256& getModificationTransactionHash() override
    {
        return m_request->m_transactionHash;
    }

private:

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
        } else
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

    // updates drive (2st phase after fsTree torrent removed)
    // - remove unused files and torrent files
    // - add new torrents to session
    //
    void continueSynchronizingDriveWithSandbox() override
    {
        DBG_BG_THREAD

        try
        {
            _LOG( "IN UPDATE 2" )

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
            _LOG( "exception during updateDrive_2: " << ex.what());
            _LOG_WARN( "exception during updateDrive_2: " << ex.what());
            finishTask();
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

    uint64_t getToBeApprovedDownloadSize() override
    {
        return m_request->m_maxDataSize;
    }
};

std::unique_ptr<DriveTaskBase> createModificationTask( mobj<ModificationRequest>&& request,
                                            std::map<std::array<uint8_t,32>,ApprovalTransactionInfo>&& receivedOpinions,
                                            DriveParams& drive,
                                            ModifyOpinionController& opinionTaskController)
{
    return std::make_unique<ModifyDriveTask>( std::move(request), std::move(receivedOpinions), drive, opinionTaskController );
}

}

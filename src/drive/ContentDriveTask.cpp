/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "drive/DownloadLimiter.h"
#include "DriveTask.h"
#include "drive/FlatDrivePaths.h"
#include "drive/FsTree.h"
#include "drive/ActionList.h"
#include "drive/FlatDrive.h"
#include "TaskContext.h"

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

using uint128_t = boost::multiprecision::uint128_t;

class ContentDriveTask : public BaseDriveTask
{

protected:

    ModifyOpinionController& m_opinionController;

    std::optional<ApprovalTransactionInfo> m_myOpinion;

    mobj<FsTree> m_sandboxFsTree;
    std::optional<InfoHash> m_sandboxRootHash;
    uint64_t m_metaFilesSize = 0;
    uint64_t m_sandboxDriveSize = 0;
    uint64_t m_fsTreeSize = 0;


    std::optional<lt_handle> m_sandboxFsTreeLtHandle;

    std::optional<lt_handle> m_downloadingLtHandle;

    bool m_sandboxCalculated;

    bool m_stopped;

public:

    void closeDrive( const DriveClosureRequest& closureRequest ) override
    {
        if ( m_stopped )
        {
            return;
        }

        breakTorrentDownloadAndRunNextTask();
    }

protected:

    virtual const Hash256& getModificationTransactionHash() = 0;

    ContentDriveTask(
            const DriveTaskType& type,
            TaskContext& drive,
            ModifyOpinionController& opinionTaskController)
            : BaseDriveTask( type, drive )
            , m_opinionController( opinionTaskController )
            , m_sandboxFsTree( FsTree{} )
            , m_sandboxCalculated( false )
            , m_stopped( false )
    {}

    virtual void myRootHashIsCalculated()
    {
        DBG_MAIN_THREAD

        _ASSERT( m_sandboxFsTree )
        _ASSERT( m_sandboxRootHash )

        if ( m_stopped )
        {
            finishTask();
            return;
        }

        // Notify
        if ( m_drive.m_dbgEventHandler )
        {
            m_drive.m_dbgEventHandler->rootHashIsCalculated(
                    m_drive.m_replicator,
                    m_drive.m_client,
                    getModificationTransactionHash(),
                    *m_sandboxRootHash );
        }

        /// (???) replace with replicators of the shard
        m_opinionController.updateCumulativeUploads( m_drive.getAllReplicators(), getNotApprovedDownloadSize(), [this]
        {
            onCumulativeUploadsUpdated();
        } );
    }

    void synchronizationIsCompleted()
    {
        m_opinionController.approveCumulativeUploads( [this]
        {
            modifyIsCompleted();
        });
    }

    virtual void modifyIsCompleted()
    {
        DBG_MAIN_THREAD

        _ASSERT( m_sandboxRootHash )

        m_drive.m_fsTree = std::move(m_sandboxFsTree);
        m_drive.m_rootHash = *m_sandboxRootHash;
        m_drive.m_fsTreeLtHandle = std::move( *m_sandboxFsTreeLtHandle );

//        m_isSynchronizing = false;

        finishTask();
    }

    void breakTorrentDownloadAndRunNextTask()
    {
        DBG_MAIN_THREAD

        m_stopped = true;

        if ( m_downloadingLtHandle )
        {
            if ( auto session = m_drive.m_session.lock(); session )
            {
                //
                // We must break torrent downloading because it could be unavailable
                //
                session->removeDownloadContext( *m_downloadingLtHandle );

                lt_handle torrentHandle = *m_downloadingLtHandle;
                m_downloadingLtHandle.reset();

                session->removeTorrentsFromSession( {torrentHandle}, [=, this]
                {
                    DBG_MAIN_THREAD
                    _LOG( "breakTorrentDownloadAndm_drive.runNextTask: torrent is removed" );

                    //TODO: move downloaded files from sandbox to drive (for catching-up only)

                    finishTask();
                } );
                _LOG( "breakTorrentDownloadAndRunNextTask: remove torrents " )
            }
        } else if ( ! isFinishCallable() )
        {
            // We have already executed all actions for modification
            _LOG( "breakTorrentDownloadAndRunNextTask: m_sandboxCalculated " )
            finishTask();
        } else
        {
            _LOG( "breakTorrentDownloadAndRunNextTask: nothing " )
            // We cannot break torrent download.
            // Therefore, we will wait the end of current task, that will call m_drive.runNextTask()
        }
    }

    // updates drive (1st step after approve)
    // - remove torrents from session
    //
    void startSynchronizingDriveWithSandbox()
    {
        DBG_MAIN_THREAD

        _ASSERT( !m_stopped )

        auto& torrentHandleMap = m_drive.m_torrentHandleMap;

        // Prepare map for detecting of used files
        for ( auto& it : torrentHandleMap )
        {
            it.second.m_isUsed = false;
        }

        // Mark used files
        markUsedFiles( *m_sandboxFsTree );

        // Prepare set<> for to be removed torrents
        std::set<lt::torrent_handle> toBeRemovedTorrents;

        // Add unused files into set<>
        for ( const auto& it : torrentHandleMap )
        {
            const UseTorrentInfo& info = it.second;
            if ( !info.m_isUsed )
            {
                if ( info.m_ltHandle.is_valid())
                {
                    toBeRemovedTorrents.insert( info.m_ltHandle );
                }
            }
        }

        // Add current fsTree torrent handle
        toBeRemovedTorrents.insert( m_drive.m_fsTreeLtHandle );

        // Remove unused torrents
        if ( auto session = m_drive.m_session.lock(); session )
        {
            session->removeTorrentsFromSession( toBeRemovedTorrents, [this]
            {
                m_drive.executeOnBackgroundThread( [this]
                                                   {
                                                       continueSynchronizingDriveWithSandbox();
                                                   } );
            } );
        }
    }

    void getSandboxDriveSizes( uint64_t& metaFilesSize, uint64_t& driveSize ) const
    {
        DBG_BG_THREAD

        std::error_code err;

        if ( fs::exists( m_drive.m_sandboxRootPath, err ))
        {
            metaFilesSize = fs::file_size( m_drive.m_sandboxFsTreeTorrent );
            driveSize = 0;
            m_sandboxFsTree->getSizes( m_drive.m_driveFolder, m_drive.m_torrentFolder, metaFilesSize,
                                       driveSize );
            driveSize += metaFilesSize;
        } else
        {
            metaFilesSize = 0;
            driveSize = 0;
        }
    }

    uint64_t sandboxFsTreeSize() const
    {
        std::error_code err;

        if ( fs::exists( m_drive.m_sandboxFsTreeFile, err ))
        {
            return fs::file_size( m_drive.m_sandboxFsTreeFile, err );
        }
        return 0;
    }

    ReplicatorList getUploaders()
    {
        auto replicators = m_drive.getAllReplicators();
        replicators.push_back( m_drive.m_client );
        return replicators;
    }

private:

    void onCumulativeUploadsUpdated()
    {
        DBG_MAIN_THREAD

        if ( m_stopped )
        {
            finishTask();
            return;
        }

        _ASSERT( m_sandboxRootHash )

        const auto& keyPair = m_drive.m_replicator.keyPair();
        SingleOpinion opinion( keyPair.publicKey().array());

        m_opinionController.fillOpinion( opinion.m_uploadLayout, opinion.m_clientUploadBytes );

        opinion.Sign( keyPair,
                      m_drive.m_driveKey,
                      getModificationTransactionHash(),
                      *m_sandboxRootHash,
                      m_fsTreeSize,
                      m_metaFilesSize,
                      m_sandboxDriveSize );

        m_myOpinion = std::optional<ApprovalTransactionInfo>{{m_drive.m_driveKey.array(),
                                                                     getModificationTransactionHash().array(),
                                                                     m_sandboxRootHash->array(),
                                                                     m_fsTreeSize,
                                                                     m_metaFilesSize,
                                                                     m_sandboxDriveSize,
                                                                     {std::move( opinion )}}};

        m_drive.executeOnBackgroundThread( [this]
                                           {
                                               DBG_BG_THREAD

                                               _ASSERT( m_myOpinion )

                                               saveSingleApprovalTransaction( m_myOpinion );

                                               m_drive.executeOnSessionThread( [this]
                                                                               {
                                                                                   myOpinionIsCreated();
                                                                               } );
                                           } );
    }

    virtual void continueSynchronizingDriveWithSandbox() = 0;

    virtual uint64_t getNotApprovedDownloadSize() = 0;

    virtual void myOpinionIsCreated() = 0;

    // Whether the finishTask can be called by the task itself
    virtual bool isFinishCallable() = 0;
};

class ModificationRequestDriveTask : public ContentDriveTask
{

private:

    const mobj<ModificationRequest> m_request;

    std::map<std::array<uint8_t,32>,ApprovalTransactionInfo> m_receivedOpinions;

    bool m_modifyUserDataReceived;
    bool m_modifyApproveTransactionSent;
    std::optional<Hash256> m_receivedModifyApproveTx;

    std::optional<boost::asio::high_resolution_timer> m_shareMyOpinionTimer;
    const int m_shareMyOpinionTimerDelayMs = 1000 * 60 * 5;

    std::optional<boost::asio::high_resolution_timer> m_modifyOpinionTimer;

public:

    ModificationRequestDriveTask(
            mobj<ModificationRequest>&& request,
            std::map<std::array<uint8_t,32>,ApprovalTransactionInfo>&& receivedOpinions,
            TaskContext& drive,
            ModifyOpinionController& opinionTaskController)
            : ContentDriveTask( DriveTaskType::MODIFICATION_REQUEST, drive, opinionTaskController )
            , m_request( std::move(request) )
            , m_receivedOpinions( std::move( receivedOpinions ))
            , m_modifyUserDataReceived( false )
            , m_modifyApproveTransactionSent( false )
    {
        _ASSERT( m_request )
    }

    void terminate() override
    {
        DBG_MAIN_THREAD

        m_modifyOpinionTimer.reset();
        m_shareMyOpinionTimer.reset();
    }

    void run() override
    {
        using namespace std::placeholders;  // for _1, _2, _3

        _ASSERT( !m_opinionController.getOpinionTrafficIdentifier());

        _LOG( "started modification" )

        m_opinionController.setOpinionTrafficIdentifier( m_request->m_transactionHash.array() );

        _LOG ("modification opinion identifier: " << m_request->m_transactionHash );

        if ( auto session = m_drive.m_session.lock(); session )
        {
            _ASSERT( m_opinionController.getOpinionTrafficIdentifier())
            m_downloadingLtHandle = session->download( DownloadContext(
                                                               DownloadContext::client_data,
                                                               std::bind( &ModificationRequestDriveTask::downloadHandler, this, _1, _2, _3, _4, _5, _6 ),
                                                               m_request->m_clientDataInfoHash,
                                                               *m_opinionController.getOpinionTrafficIdentifier(),
                                                               0, //todo
                                                               "" ),
                                                       m_drive.m_sandboxRootPath,
                                                       getUploaders());
        }
    }

    bool processedModifyOpinion(
            const ApprovalTransactionInfo& anOpinion ) override
    {
        // In this case Replicator is able to verify all data in the opinion
        if ( m_myOpinion &&
             m_request->m_transactionHash.array() == anOpinion.m_modifyTransactionHash &&
             validateOpinion( anOpinion ))
        {
            m_receivedOpinions[anOpinion.m_opinions[0].m_replicatorKey] = anOpinion;
            checkOpinionNumberAndStartTimer();
        }
        return true;
    }

    bool shouldCatchUp(
            const PublishedModificationApprovalTransactionInfo& transaction ) override
    {
        if ( m_stopped )
        {
            return true;
        }

        if ( m_request->m_transactionHash == transaction.m_modifyTransactionHash
             && m_modifyUserDataReceived )
        {
            if ( m_sandboxCalculated )
            {
                const auto& v = transaction.m_replicatorKeys;
                auto it = std::find( v.begin(), v.end(), m_drive.m_replicator.replicatorKey().array());

                // Is my opinion present
                if ( it == v.end())
                {
                    // Send Single Approval Transaction At First
                    sendSingleApprovalTransaction( *m_myOpinion );
                }

                startSynchronizingDriveWithSandbox();
            }
            return false;
        } else
        {
            m_opinionController.increaseApprovedExpectedCumulativeDownload(m_request->m_maxDataSize);
            breakTorrentDownloadAndRunNextTask();
            return true;
        }
    }

    bool shouldCancelModify( const ModificationCancelRequest& cancelRequest ) override
    {
        if ( m_stopped )
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

    void approvalTransactionHasFailed( const Hash256& transactionHash ) override
    {
        DBG_MAIN_THREAD

        if ( m_request->m_transactionHash == transactionHash &&
             !m_stopped &&
             !m_receivedModifyApproveTx )
        {
            m_modifyApproveTransactionSent = false;
            for ( const auto&[key, opinion]: m_receivedOpinions )
            {
                m_drive.m_replicator.processOpinion( opinion );
            }
            m_receivedOpinions.clear();
        }
    }

private:

    // will be called by Session
    void downloadHandler( download_status::code code,
                          const InfoHash& infoHash,
                          const std::filesystem::path /*filePath*/,
                          size_t /*downloaded*/,
                          size_t /*fileSize*/,
                          const std::string& errorText )
    {
        DBG_MAIN_THREAD

        _ASSERT( m_request->m_clientDataInfoHash == infoHash )

        if ( code == download_status::failed )
        {
            if ( m_drive.m_dbgEventHandler )
            {
                m_drive.m_dbgEventHandler->modifyTransactionEndedWithError( m_drive.m_replicator, m_drive.m_driveKey,
                                                                            *m_request,
                                                                            errorText, 0 );
            }
            modifyIsCompleted();
            return;
        }

        if ( code == download_status::download_complete )
        {
            _ASSERT( !m_stopped );

            m_downloadingLtHandle.reset();

            m_modifyUserDataReceived = true;

            m_drive.executeOnBackgroundThread( [this]
                                               {
                                                   modifyDriveInSandbox();
                                               } );
        }
    }

    void modifyDriveInSandbox()
    {
        DBG_BG_THREAD

        std::error_code err;

        // Check that client data exist
        if ( !fs::exists( m_drive.m_clientDataFolder, err ) ||
             !fs::is_directory( m_drive.m_clientDataFolder, err ))
        {
            _LOG_ERR( "modifyDriveInSandbox: 'client-data' is absent; m_clientDataFolder="
                              << m_drive.m_clientDataFolder );
            if ( m_drive.m_dbgEventHandler )
            {
                m_drive.m_dbgEventHandler->modifyTransactionEndedWithError( m_drive.m_replicator, m_drive.m_driveKey,
                                                                            *m_request,
                                                                            "modify drive: 'client-data' is absent",
                                                                            -1 );
            }
            m_drive.executeOnSessionThread( [=, this]
                                            {
                                                modifyIsCompleted();
                                            } );
            return;
        }

        // Check 'actionList.bin' is received
        if ( !fs::exists( m_drive.m_clientActionListFile, err ))
        {
            _LOG_ERR( "modifyDriveInSandbox: 'ActionList.bin' is absent: "
                              << m_drive.m_clientActionListFile );
            if ( m_drive.m_dbgEventHandler )
            {
                m_drive.m_dbgEventHandler->modifyTransactionEndedWithError( m_drive.m_replicator, m_drive.m_driveKey,
                                                                            *m_request,
                                                                            "modify drive: 'ActionList.bin' is absent",
                                                                            -1 );
            }
            m_drive.executeOnSessionThread( [=, this]()
                                            {
                                                modifyIsCompleted();
                                            } );
            return;
        }

        // Load 'actionList' into memory
        ActionList actionList;
        actionList.deserialize( m_drive.m_clientActionListFile );

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
                    // Check that file exists in client folder
                    fs::path clientFile = m_drive.m_clientDriveFolder / action.m_param2;
                    if ( !fs::exists( clientFile, err ) || fs::is_directory( clientFile, err ))
                    {
                        _LOG( "! ActionList: invalid 'upload': file/folder not exists: " << clientFile )
                        action.m_isInvalid = true;
                        break;
                    }

                    try
                    {
                        // calculate torrent, file hash, and file size
                        InfoHash fileHash = calculateInfoHashAndCreateTorrentFile( clientFile, m_drive.m_driveKey,
                                                                                   m_drive.m_torrentFolder,
                                                                                   "" );
                        size_t fileSize = std::filesystem::file_size( clientFile );

                        // add ref into 'torrentMap' (skip if identical file was already loaded)
                        torrentHandleMap.try_emplace( fileHash, UseTorrentInfo{} );

                        // rename file and move it into drive folder
                        std::string newFileName = m_drive.m_driveFolder / hashToFileName( fileHash );
                        fs::rename( clientFile, newFileName );

                        //
                        // add file in resultFsTree
                        //
                        Folder::Child* destEntry = m_sandboxFsTree->getEntryPtr( action.m_param2 );
                        fs::path destFolder;
                        fs::path srcFile;
                        if ( destEntry != nullptr && isFolder( *destEntry ))
                        {
                            srcFile = fs::path( action.m_param1 ).filename();
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

        m_drive.executeOnSessionThread( [=, this]() mutable
                                        {
                                            myRootHashIsCalculated();
                                        } );
    }

    bool isFinishCallable() override
    {
        return !m_sandboxCalculated;
    }

protected:

    void modifyIsCompleted() override
    {
        _LOG( "modifyIsCompleted" );
        m_drive.m_dbgEventHandler->driveModificationIsCompleted( m_drive.m_replicator, m_drive.m_driveKey,
                                                                 m_request->m_transactionHash,
                                                                 *m_sandboxRootHash );
        ContentDriveTask::modifyIsCompleted();
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

        if ( m_stopped )
        {
            finishTask();
            return;
        }

        m_sandboxCalculated = true;

        if ( m_receivedModifyApproveTx )
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
                    shareMyOpinion();
                } );
            }

            if ( m_dbgOurPeerName == "replicator_04" )
            {
                _LOG( "calculated" )
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
             !m_receivedModifyApproveTx )
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

        if ( m_modifyApproveTransactionSent || m_receivedModifyApproveTx )
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
                if ( !it.second.m_ltHandle.is_valid())
                {
                    if ( auto session = m_drive.m_session.lock(); session )
                    {
                        std::string fileName = hashToFileName( it.first );
                        it.second.m_ltHandle = session->addTorrentFileToSession(
                                m_drive.m_torrentFolder / fileName,
                                m_drive.m_driveFolder,
                                lt::sf_is_replicator );
                    }
                }
            }

            // Add FsTree torrent to session
            if ( auto session = m_drive.m_session.lock(); session )
            {
                m_sandboxFsTreeLtHandle = session->addTorrentFileToSession( m_drive.m_fsTreeTorrent,
                                                                            m_drive.m_fsTreeTorrent.parent_path(),
                                                                            lt::sf_is_replicator );
            }

            m_drive.executeOnSessionThread( [this]() mutable
                                            {
                                                synchronizationIsCompleted();
                                            } );
        }
        catch (const std::exception& ex)
        {
            _LOG_ERR( "exception during updateDrive_2: " << ex.what());
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

    uint64_t getNotApprovedDownloadSize() override
    {
        return m_request->m_maxDataSize;
    }
};

class CatchingUpTask : public ContentDriveTask
{

private:

    const mobj<CatchingUpRequest> m_request;

    std::set<InfoHash> m_catchingUpFileSet;
    std::set<InfoHash>::iterator m_catchingUpFileIt = m_catchingUpFileSet.end();

public:

    CatchingUpTask( mobj<CatchingUpRequest>&& request,
                    TaskContext& drive,
                    ModifyOpinionController& opinionTaskController )
                    : ContentDriveTask(DriveTaskType::CATCHING_UP, drive, opinionTaskController)
                    , m_request( std::move(request) )
    {
        _ASSERT( m_request )
    }

    void run() override
    {
        DBG_MAIN_THREAD

        if ( m_request->m_rootHash == m_drive.m_rootHash )
        {
            finishTask();
            return;
        }

        _LOG( "started catching up" )

        //
        // Start download fsTree
        //
        using namespace std::placeholders;  // for _1, _2, _3

        _LOG( "Late: download FsTree:" << m_request->m_rootHash )

        if ( !m_opinionController.getOpinionTrafficIdentifier())
        {
            m_opinionController.setOpinionTrafficIdentifier( m_request->m_modifyTransactionHash.array() );

            _LOG ("catching up opinion identifier: " << m_request->m_modifyTransactionHash );
        }

        if ( auto session = m_drive.m_session.lock(); session )
        {
            _ASSERT( m_opinionController.getOpinionTrafficIdentifier())
            m_downloadingLtHandle = session->download( DownloadContext(
                                                               DownloadContext::missing_files,
                                                               std::bind( &CatchingUpTask::catchingUpFsTreeDownloadHandler, this, _1, _2, _3, _4, _5, _6 ),
                                                               m_request->m_rootHash,
                                                               *m_opinionController.getOpinionTrafficIdentifier(),
                                                               0,
                                                               "" ),
                    //toString( *m_catchingUpRootHash ) ),
                                                       m_drive.m_sandboxRootPath,
                    //{} );
                                                       getUploaders());
        }
    }

    void terminate() override
    {

    }

    bool shouldCatchUp( const PublishedModificationApprovalTransactionInfo& transaction ) override
    {
        if ( m_stopped )
        {
            return true;
        }

        if ( m_request->m_rootHash == transaction.m_rootHash )
        {
            // TODO We should update knowledge about the catching modification id
            // This situation could be valid if some next modification has not changed Drive Root Hash
            // For example, because of next modification was invalid
            // So, we continue previous catching-up
            return false;
        } else
        {
            breakTorrentDownloadAndRunNextTask();
            return true;
        }
    }

protected:

    const Hash256& getModificationTransactionHash() override
    {
        return m_request->m_modifyTransactionHash;
    }

    void modifyIsCompleted() override
    {
        _LOG( "catchingIsCompleted" );
        m_drive.m_dbgEventHandler->driveModificationIsCompleted( m_drive.m_replicator, m_drive.m_driveKey,
                                                                 m_request->m_modifyTransactionHash,
                                                                 *m_sandboxRootHash );
        ContentDriveTask::modifyIsCompleted();
    }

private:

    void continueSynchronizingDriveWithSandbox() override
    {
        DBG_BG_THREAD

        try
        {
            //
            // Check RootHash Before All
            //
            _LOG( "m_sandboxRootHash: " << *m_sandboxRootHash );
            _LOG( "m_catchingUpRootHash: " << m_request->m_rootHash );
            _ASSERT( m_sandboxRootHash == m_request->m_rootHash );

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

            //
            // Add missing files
            //
            for ( const auto& fileHash : m_catchingUpFileSet )
            {
                auto fileName = toString( fileHash );

                // Add torrent into session
                if ( auto session = m_drive.m_session.lock(); session )
                {
                    auto tHandle = session->addTorrentFileToSession( m_drive.m_torrentFolder / fileName,
                                                                     m_drive.m_driveFolder,
                                                                     lt::sf_is_replicator );
                    _ASSERT( tHandle.is_valid());
                    torrentHandleMap.try_emplace( fileHash, UseTorrentInfo{tHandle, true} );
                }
            }

            // Add FsTree torrent to session
            if ( auto session = m_drive.m_session.lock(); session )
            {
                m_sandboxFsTreeLtHandle = session->addTorrentFileToSession( m_drive.m_fsTreeTorrent,
                                                                            m_drive.m_fsTreeTorrent.parent_path(),
                                                                            lt::sf_is_replicator );
            }

            // remove unused data from 'torrentMap'
            std::erase_if( torrentHandleMap, []( const auto& it )
            { return !it.second.m_isUsed; } );

            LOG( "drive is synchronized" );

            m_drive.executeOnSessionThread( [=, this]
                                            {
                                                synchronizationIsCompleted();
                                            } );
        }
        catch (const std::exception& ex)
        {
            _LOG_ERR( "exception during completeCatchingUp: " << ex.what());
            finishTask();
        }
    }

    void myOpinionIsCreated() override
    {
        DBG_MAIN_THREAD

        _ASSERT( m_myOpinion )

        if ( m_stopped )
        {
            finishTask();
            return;
        }

        m_sandboxCalculated = true;

        sendSingleApprovalTransaction( *m_myOpinion );

        startSynchronizingDriveWithSandbox();
    }

    // it will be called from Session
    void catchingUpFsTreeDownloadHandler( download_status::code code,
                                          const InfoHash& infoHash,
                                          const std::filesystem::path /*filePath*/,
                                          size_t /*downloaded*/,
                                          size_t /*fileSize*/,
                                          const std::string& errorText )
    {
        DBG_MAIN_THREAD

        _ASSERT( !m_stopped );

        if ( code == download_status::failed )
        {
            //todo is it possible?
            _ASSERT( 0 );
            return;
        }

        if ( code == download_status::download_complete )
        {
            m_downloadingLtHandle.reset();
            startDownloadMissingFiles();
        }
    }

    void startDownloadMissingFiles()
    {
        DBG_MAIN_THREAD

        _LOG( "startDownloadMissingFiles: removeTorrentsFromSession: " << m_downloadingLtHandle->id() << " "
                                                                       << m_downloadingLtHandle->info_hashes().v2 );

        //
        // Deserialize FsTree
        //
        try
        {
            m_sandboxFsTree->deserialize( m_drive.m_sandboxFsTreeFile );
        }
        catch (...)
        {
            _LOG_ERR( "cannot deserialize 'CatchingUpFsTree'" );
        }

        //
        // Prepare missing list and start download
        //
        m_catchingUpFileSet.clear();
        createCatchingUpFileList( *m_sandboxFsTree );

        m_catchingUpFileIt = m_catchingUpFileSet.begin();
        downloadMissingFiles();
    }

    void createCatchingUpFileList( const Folder& folder )
    {
        DBG_MAIN_THREAD

        for ( const auto& child : folder.childs() )
        {
            if ( isFolder( child ))
            {
                createCatchingUpFileList( getFolder( child ));
            } else
            {
                const auto& hash = getFile( child ).hash();
                std::error_code err;

                if ( !fs::exists( m_drive.m_driveFolder / toString( hash ), err ))
                {
                    m_catchingUpFileSet.emplace( hash );
                }
            }
        }
    }

    // Download file by file
    void downloadMissingFiles()
    {
        DBG_MAIN_THREAD

        _ASSERT( !m_stopped );

        if ( m_catchingUpFileIt == m_catchingUpFileSet.end())
        {
            m_downloadingLtHandle.reset();

            // it is the end of list
            m_drive.executeOnBackgroundThread( [this]
                                               {
                                                   modifyDriveInSandbox();
                                               } );
        } else
        {
//            if ( m_newCatchingUpRequest && m_newCatchingUpRequest->m_rootHash == m_catchingUpRequest->m_rootHash )
//            {
//                // TODO Check this situation
//                _LOG_ERR( "Not Implemented" );
//                return;
//            }

            auto missingFileHash = *m_catchingUpFileIt;
            m_catchingUpFileIt++;

            if ( auto session = m_drive.m_session.lock(); session )
            {
                _ASSERT( m_opinionController.getOpinionTrafficIdentifier())
                m_downloadingLtHandle = session->download( DownloadContext(

                                                                   DownloadContext::missing_files,

                                                                   [this]( download_status::code code,
                                                                           const InfoHash& infoHash,
                                                                           const std::filesystem::path saveAs,
                                                                           size_t /*downloaded*/,
                                                                           size_t /*fileSize*/,
                                                                           const std::string& errorText )
                                                                   {
                                                                       if ( code == download_status::download_complete )
                                                                       {
                                                                           _LOG( "catchedUp: " << toString( infoHash ));
                                                                           downloadMissingFiles();
                                                                       } else if ( code == download_status::failed )
                                                                       {
                                                                           _LOG_ERR( "? is it possible now?" );
                                                                       }
                                                                   },

                                                                   missingFileHash,
                                                                   *m_opinionController.getOpinionTrafficIdentifier(),
                                                                   0,
                                                                   "" ),
                                                           m_drive.m_sandboxRootPath,
                                                           getUploaders());
            }
        }
    }

    void modifyDriveInSandbox()
    {
        DBG_BG_THREAD

        for ( const auto& fileHash : m_catchingUpFileSet )
        {
            auto fileName = toString( fileHash );

            // move file to drive folder
            try
            {
                fs::rename( m_drive.m_sandboxRootPath / fileName, m_drive.m_driveFolder / fileName );
            }
            catch (const std::exception& ex)
            {
                _LOG( "exception during rename:" << ex.what());
                _LOG_ERR( "exception during rename '" << m_drive.m_sandboxRootPath / fileName <<
                                                      "' to '" << m_drive.m_driveFolder / fileName << "'; "
                                                      << ex.what());
            }

            // create torrent
            calculateInfoHashAndCreateTorrentFile( m_drive.m_driveFolder / fileName,
                                                   m_drive.m_driveKey,
                                                   m_drive.m_torrentFolder, "" );
        }

        // create FsTree in sandbox
        m_sandboxFsTree->doSerialize( m_drive.m_sandboxFsTreeFile );

        m_sandboxRootHash = createTorrentFile( m_drive.m_sandboxFsTreeFile,
                                               m_drive.m_driveKey,
                                               m_drive.m_sandboxRootPath,
                                               m_drive.m_sandboxFsTreeTorrent );

        getSandboxDriveSizes( m_metaFilesSize, m_sandboxDriveSize );
        m_fsTreeSize = sandboxFsTreeSize();

        m_drive.executeOnSessionThread( [=, this]() mutable
                                        {
                                            myRootHashIsCalculated();
                                        } );
    }

    uint64_t getNotApprovedDownloadSize() override
    {
        return 0;
    }

    bool isFinishCallable() override
    {
        return true;
    }
};

std::unique_ptr<BaseDriveTask> createModificationTask( mobj<ModificationRequest>&& request,
                                            std::map<std::array<uint8_t,32>,ApprovalTransactionInfo>&& receivedOpinions,
                                            TaskContext& drive,
                                            ModifyOpinionController& opinionTaskController)
{
//    auto s = ModificationRequestDriveTask(request, receivedOpinions, drive, opinionTaskController);
//    mobj<ModificationRequestDriveTask> h
//    = mobj<ModificationRequestDriveTask>( request, receivedOpinions, drive, opinionTaskController );
    return std::make_unique<ModificationRequestDriveTask>( std::move(request), std::move(receivedOpinions), drive, opinionTaskController );
}

std::unique_ptr<BaseDriveTask> createCatchingUpTask( mobj<CatchingUpRequest>&& request,
                                                     TaskContext& drive,
                                                     ModifyOpinionController& opinionTaskController )
{
    return std::make_unique<CatchingUpTask>( std::move(request), drive, opinionTaskController);
}

}

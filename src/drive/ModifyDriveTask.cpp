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
#include "ModifyTaskBase.h"

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

class ModifyDriveTask : public ModifyTaskBase
{

private:

    const mobj<ModificationRequest> m_request;

    std::set<InfoHash> m_missedFileSet;

    bool m_actionListIsReceived = false;

public:

    ModifyDriveTask(
            mobj<ModificationRequest>&& request,
            std::map<std::array<uint8_t,32>,ApprovalTransactionInfo>&& receivedOpinions,
            DriveParams& drive,
            ModifyOpinionController& opinionTaskController)
            : ModifyTaskBase( DriveTaskType::MODIFICATION_REQUEST, drive, std::move(receivedOpinions), opinionTaskController )
            , m_request( std::move(request) )
    {
        SIRIUS_ASSERT( m_request )
    }

    void shutdown() override
    {
        DBG_MAIN_THREAD

        m_modifyOpinionTimer.cancel();
        m_shareMyOpinionTimer.cancel();

        interruptTorrentDownloadAndRunNextTask();
    }

    void run() override
    {
        DBG_MAIN_THREAD

        m_uploadedDataSize = 0;

        //_LOG( "?????????: " << m_request->m_clientDataInfoHash  << "   " << m_drive.m_torrentHandleMap.size() )
        if ( auto it = m_drive.m_torrentHandleMap.find( m_request->m_clientDataInfoHash ); it != m_drive.m_torrentHandleMap.end() )
        {
            SIRIUS_ASSERT( 0 )
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
                                                           SIRIUS_ASSERT( 0 );
                                                           m_drive.m_torrentHandleMap.erase( infoHash );
                                                           modifyIsCompletedWithError( errorText, ModificationStatus::DOWNLOAD_FAILED );
                                                       }
                                                       else if ( code == download_status::download_complete )
                                                       {
                                                           //SIRIUS_ASSERT( !m_taskIsStopped );
                                                           // it could be stopped after asyncApprovalTransactionHasBeenPublished
                                                           // if 'actionList' have not been downloaded

                                                           if ( ! m_taskIsInterrupted )
                                                           {
                                                               // remeber "fsTree or actionList handle"; they to be removed in finishTask()
                                                               m_fsTreeOrActionListHandle = m_downloadingLtHandle;
                                                               
                                                               m_downloadingLtHandle.reset();
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
                                               m_drive.m_sandboxRootPath / toPath((toString(m_request->m_clientDataInfoHash)) + ".torrent"),
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

        bool actionListExists = fs::exists( actionListFilename, err );

        if (err)
        {
            _LOG_ERR("prepareDownloadMissingFiles action list fs error: " << err.message());
            return;
        }
        else if ( !actionListExists )
        {
            _LOG_ERR("prepareDownloadMissingFiles action list does not exist");
            return;
        }

        // Load 'actionList' into memory
        ActionList actionList;
        try {
            actionList.deserialize( actionListFilename );
        } catch (...)
        {
            _LOG_WARN( "modifyDriveInSandbox: invalid 'ActionList'" << m_request->m_clientDataInfoHash );
            m_drive.executeOnSessionThread( [this] { modifyIsCompletedWithError( "modify drive: invalid 'ActionList'", ModificationStatus::INVALID_ACTION_LIST ); } );
        }
        
        // prepare 'm_missedFileSet'
        for ( const Action& action : actionList )
        {
            if ( action.m_isInvalid )
                continue;

            switch (action.m_actionId)
            {
                case action_list_id::upload:
                    m_missedFileSet.insert( stringToByteArray<Hash256>( action.m_param1 ) );
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

        SIRIUS_ASSERT( !m_taskIsInterrupted );

        std::optional<Hash256> fileToDownload;

        while ( !m_missedFileSet.empty() && !fileToDownload )
        {
            fileToDownload = *m_missedFileSet.begin();
            m_missedFileSet.erase( m_missedFileSet.begin());

            if ( auto it = m_drive.m_torrentHandleMap.find( *fileToDownload ); *fileToDownload == Hash256() ||
                                                                               it != m_drive.m_torrentHandleMap.end()) {
                SIRIUS_ASSERT( it->second.m_ltHandle.is_valid())
                fileToDownload.reset();
            }
        }

        if ( fileToDownload )
        {
            if ( auto session = m_drive.m_session.lock(); session )
            {
                SIRIUS_ASSERT( *fileToDownload != Hash256() )
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
                                                                           m_downloadingLtHandle.reset();
                                                                           downloadMissingFiles();
                                                                       }
                                                                       else if ( code == download_status::dn_failed )
                                                                       {
                                                                           SIRIUS_ASSERT( 0 );
                                                                           m_drive.m_torrentHandleMap.erase( infoHash );
                                                                           modifyIsCompletedWithError( errorText, ModificationStatus::DOWNLOAD_FAILED );
                                                                       }
                                                                   },

                                                                   *fileToDownload,
                                                                   m_request->m_transactionHash,
                                                                   m_request->m_maxDataSize - m_uploadedDataSize,
                                                                   true,
                                                                   "" ),
                                                           m_drive.m_driveFolder,
                                                           m_drive.m_torrentFolder / toString(*fileToDownload),
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
            if ( m_downloadingLtHandle )
            {
                _LOG( "Download Handle Reset" )
                m_downloadingLtHandle.reset();
            }

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
        SIRIUS_ASSERT( m_drive.m_fsTree )
        m_sandboxFsTree = std::make_unique<FsTree>( *m_drive.m_fsTree );

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
                        auto fileHash = stringToByteArray<Hash256>( action.m_param1 );
                        
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
                        m_sandboxFsTree->addFile( destFolder.string(),
                                                  srcFile.string(),
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
                        std::error_code ec;
                        bool isPathInside = isPathInsideFolder( srcPath, destPath, ec );
                        if (ec)
                        {
                            _LOG( "ModifyDriveTask::modifyFsTreeInSandbox. isPathInsideFolder error: " << ec.message() << " code: " << ec.value()
                                                                                                       << " srcPath: " << srcPath.string()
                                                                                                       << " destPath: " << destPath.string() )
                            break;
                        }

                        if ( isPathInside )
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
                        if ( fileHash != Hash256())
                        {
                            torrentHandleMap.try_emplace( fileHash, UseTorrentInfo{} );
                        }
                    } );

                    _LOG( "! ActionList: successful 'remove': " << action.m_param1 );
                    break;
                }

            } // end of switch()
        } // end of for( const Action& action : actionList )

        // create FsTree in sandbox
        m_sandboxFsTree->doSerialize( m_drive.m_sandboxFsTreeFile.string() );

        m_sandboxRootHash = createTorrentFile( m_drive.m_sandboxFsTreeFile,
                                               m_drive.m_driveKey,
                                               m_drive.m_sandboxRootPath,
                                               m_drive.m_sandboxFsTreeTorrent );

        getSandboxDriveSizes( m_metaFilesSize, m_sandboxDriveSize );
        m_fsTreeSize = sandboxFsTreeSize();

        if ( m_metaFilesSize + m_sandboxDriveSize + m_fsTreeSize > m_drive.m_maxSize )
        {
            m_drive.executeOnSessionThread( [this] {
                modifyIsCompletedWithError( "Drive is full", ModificationStatus::NOT_ENOUGH_SPACE );
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
        if ( m_request->m_transactionHash.array() != anOpinion.m_modifyTransactionHash )
        {
            return false;
        }
        if ( m_myOpinion )
        {
            if ( validateOpinion( anOpinion ) )
            {
                m_receivedOpinions[anOpinion.m_opinions[0].m_replicatorKey] = anOpinion;
                sendModifyApproveTxWithDelay();
            }
         }
        else {
            m_receivedOpinions[anOpinion.m_opinions[0].m_replicatorKey] = anOpinion;
        }
        return true;
    }

    // Returns 'true' if 'CatchingUp' should be started

    bool onApprovalTxPublished( const PublishedModificationApprovalTransactionInfo& transaction ) override
    {
        DBG_MAIN_THREAD
        
        if ( m_taskIsInterrupted )
        {
            return true;
        }

        m_modifyApproveTxReceived = true;

        // Note: approvalTx have been received, it means that other nodes can 'remove action list'
        // If our node did not receive 'actionList', we need start syncing
        // If our node received 'actionList', then we will continue download missed files
        //
        // 'm_modificationStatus != ModificationStatus::SUCCESS' means
        // that all nodes completed modification with the same error (the error is absent in ApprovalTransactionInfo!)
        // so, we need send single tx with this error
        //
        if ( m_request->m_transactionHash == transaction.m_modifyTransactionHash
            && ( m_actionListIsReceived || m_modificationStatus != ModificationStatus::SUCCESS ))
        {
            if ( !m_sandboxCalculated )
            {
                // it means our task currently works on BG thread
                // so, it must be completed/ended
                return false;
            }

            // Current our state: we are waiting the approval tx
            //SIRIUS_ASSERT( m_sandboxCalculated )
            
			if ( *m_sandboxRootHash != transaction.m_rootHash ) {
				_LOG_ERR( "Invalid Sandbox Root Hash: " << *m_sandboxRootHash << " " << Hash256(transaction.m_rootHash) )
			}
            
            // Is my opinion present in received transaction?
            //
            const auto& v = transaction.m_replicatorKeys;
            if ( auto it = std::find( v.begin(), v.end(), m_drive.m_replicator.replicatorKey().array());
                it == v.end())
            {
                // Send Single Approval Transaction At First
                sendSingleApprovalTransaction( *m_myOpinion );
            }

            // Accept modification
            // or clear unused files in case of 'm_modificationStatus != ModificationStatus::SUCCESS'
            startSynchronizingDriveWithSandbox();
            return false;
        }
        else
        {
            // Our lag (rus: 'otstavanie') is to big
            // we must start syncing
            //
            m_opinionController.increaseApprovedExpectedCumulativeDownload(m_request->m_maxDataSize);
            interruptTorrentDownloadAndRunNextTask();
            return true;
        }
    }

    void interruptTask( const ModificationCancelRequest& cancelRequest, bool& cancelRequestIsAccepted ) override
    {
        DBG_MAIN_THREAD
        
        if ( ! m_taskIsInterrupted &&
             cancelRequest.m_modifyTransactionHash == m_request->m_transactionHash )
        {
            interruptTorrentDownloadAndRunNextTask();
            cancelRequestIsAccepted = true;
            return;
        }

        cancelRequestIsAccepted = false;
    }

    void onApprovalTxFailed( const Hash256& transactionHash ) override
    {
        DBG_MAIN_THREAD

        if ( m_request->m_transactionHash == transactionHash &&
             !m_taskIsInterrupted &&
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
    
    void tryFinishTask() override
    {
        if ( m_sandboxCalculated && ! m_modifyApproveTxReceived )
        {
            SIRIUS_ASSERT( ! m_downloadingLtHandle )
            removeTorrentsAndFinishTask();
        }
        else
        {
            // we will wait the end of the current task, that will call m_drive.runNextTask()
        }
    }

protected:

    void modifyIsCompletedWithError( std::string errorText, ModificationStatus status )
    {
        DBG_MAIN_THREAD

        SIRIUS_ASSERT( status != ModificationStatus::SUCCESS )

		_LOG( "modifyIsCompletedWithError " << errorText << " " << static_cast<uint8_t>(status) );

        if ( m_drive.m_dbgEventHandler )
        {
            m_drive.m_dbgEventHandler->modifyTransactionEndedWithError(
                                         m_drive.m_replicator,
                                         m_drive.m_driveKey,
                                         *m_request,
                                         errorText, static_cast<uint8_t>(status) );
        }

        m_downloadingLtHandle.reset();

        m_modificationStatus = status;

        m_drive.executeOnBackgroundThread([this]
        {
            SIRIUS_ASSERT( m_drive.m_fsTree )
            m_sandboxRootHash = m_drive.m_rootHash;
            m_sandboxFsTree = std::make_unique<FsTree>( *m_drive.m_fsTree );
            std::error_code ec;
            fs::remove( m_drive.m_sandboxFsTreeFile, ec );
            fs::copy( m_drive.m_fsTreeFile, m_drive.m_sandboxFsTreeFile );
            fs::remove( m_drive.m_sandboxFsTreeTorrent, ec );
            fs::copy( m_drive.m_fsTreeTorrent, m_drive.m_sandboxFsTreeTorrent );

            m_drive.executeOnSessionThread([this]
            {
                myRootHashIsCalculated();
            });
        });
    }

    const Hash256& getModificationTransactionHash() override
    {
        return m_request->m_transactionHash;
    }

private:

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

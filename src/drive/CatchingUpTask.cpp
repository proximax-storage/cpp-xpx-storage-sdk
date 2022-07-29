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

class CatchingUpTask : public UpdateDriveTaskBase
{
private:

    mobj<CatchingUpRequest>      m_request;

    std::set<InfoHash>           m_catchingUpFileSet;
    std::set<InfoHash>::iterator m_catchingUpFileIt = m_catchingUpFileSet.end();

public:

    CatchingUpTask( mobj<CatchingUpRequest>&& request,
                    DriveParams& drive,
                    ModifyOpinionController& opinionTaskController )
                    : UpdateDriveTaskBase(DriveTaskType::CATCHING_UP, drive, opinionTaskController)
                    , m_request( std::move(request) )
    {
        _ASSERT( m_request )
    }
    
    void terminate() override
    {
        DBG_MAIN_THREAD

        breakTorrentDownloadAndRunNextTask();
    }

    void run() override
    {
        DBG_MAIN_THREAD

        if ( m_drive.m_lastApprovedModification == m_request->m_modifyTransactionHash )
        {
            _ASSERT( m_drive.m_rootHash == m_request->m_rootHash );
            finishTask();
            return;
        }

        if ( ! m_opinionController.opinionTrafficTx() )
        {
            m_opinionController.setOpinionTrafficTx( m_request->m_modifyTransactionHash.array() );

            _LOG ("catching up opinion identifier: " << m_request->m_modifyTransactionHash );
        }

        if ( m_request->m_rootHash == m_drive.m_rootHash )
        {
            _LOG( "No need to catch up to " << m_request->m_rootHash )

            m_drive.executeOnBackgroundThread( [this]
            {
                m_sandboxRootHash = m_request->m_rootHash;
                m_sandboxRootHash = m_drive.m_rootHash;
                m_sandboxFsTree = std::make_unique<FsTree>();
                m_sandboxFsTree->deserialize( m_drive.m_fsTreeFile.string() );
                std::error_code ec;
                fs::remove( m_drive.m_sandboxFsTreeFile, ec );
                fs::copy( m_drive.m_fsTreeFile, m_drive.m_sandboxFsTreeFile );
                fs::remove( m_drive.m_sandboxFsTreeTorrent, ec );
                fs::copy( m_drive.m_fsTreeTorrent, m_drive.m_sandboxFsTreeTorrent );
                modifyDriveInSandbox();
            });
            return;
        }

        _LOG( "started catching up" )

        //
        // Start download fsTree
        //
        if ( auto session = m_drive.m_session.lock(); session )
        {
            _ASSERT( m_opinionController.opinionTrafficTx())
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

                               _ASSERT( !m_taskIsInterrupted );

                               if ( code == download_status::dn_failed )
                               {
                                   //todo is it possible?
                                   _ASSERT( 0 );
                                   return;
                               }

                               if ( code == download_status::download_complete )
                               {
                                   m_sandboxRootHash = infoHash;
                                   m_downloadingLtHandle.reset();
                                   m_drive.executeOnBackgroundThread([this] {
                                       try
                                       {
                                           m_sandboxFsTree = std::make_unique<FsTree>();
                                           m_sandboxFsTree->deserialize( m_drive.m_sandboxFsTreeFile.string() );
                                           m_sandboxFsTree->dbgPrint();
                                       }
                                       catch (...)
                                       {
                                           _LOG_ERR( "cannot deserialize 'CatchingUpFsTree'" );
                                           return;
                                       }

                                       m_drive.executeOnSessionThread([this] {
                                           createUnusedFileList();
                                       });
                                   });
                               }
                           },
                           m_request->m_rootHash,
                           *m_opinionController.opinionTrafficTx(),
                           0, true, m_drive.m_sandboxFsTreeFile
                   ),
                   m_drive.m_sandboxRootPath.string(),
                   m_drive.m_sandboxFsTreeTorrent.string(),
                   getUploaders(),
                   &m_drive.m_driveKey.array(),
                   nullptr,
                   &m_opinionController.opinionTrafficTx().value().array() );
            m_drive.m_torrentHandleMap[m_request->m_rootHash] = { *m_downloadingLtHandle, false };
        }
    }

    bool shouldCancelModify( const ModificationCancelRequest& cancelRequest ) override {

        DBG_MAIN_THREAD

        // It is a very rare situation:
        // After the initialization we are running the task
        // Without actually need to catch up
        // During the task is beeing finished the cancel is requested
        if ( cancelRequest.m_modifyTransactionHash == m_opinionController.notApprovedModificationId() )
        {
            _ASSERT( cancelRequest.m_modifyTransactionHash != m_request->m_modifyTransactionHash )
            _ASSERT( m_drive.m_lastApprovedModification == m_request->m_modifyTransactionHash )
            return true;
        }

        return false;
    }

    void createUnusedFileList()
    {
        DBG_MAIN_THREAD

        _ASSERT( m_sandboxFsTree )

        //
        // Create list/set of unused files
        //
        auto& torrentHandleMap = m_drive.m_torrentHandleMap;

        for( auto& it : torrentHandleMap )
        {
            it.second.m_isUsed = false;
        }

        markUsedFiles( *m_sandboxFsTree );
        
        // Prepare set<> for to be removed torrents
        std::set<lt::torrent_handle> toBeRemovedTorrents;

        // Add unused files into set<>
        for ( const auto& it : torrentHandleMap )
        {
            const UseTorrentInfo& info = it.second;
            if ( ! info.m_isUsed )
            {
                if ( info.m_ltHandle.is_valid() )
                {
                    toBeRemovedTorrents.insert( info.m_ltHandle );
                }
            }
        }

        // Remove unused torrents
        if ( auto session = m_drive.m_session.lock(); session )
        {
            session->removeTorrentsFromSession( toBeRemovedTorrents, [this]
            {
                std::set<InfoHash> filesToRemove;

                for ( auto it = m_drive.m_torrentHandleMap.begin(); it != m_drive.m_torrentHandleMap.end(); )
                {
                    if ( ! it->second.m_isUsed )
                    {
                        filesToRemove.insert( it->first );
                        it = m_drive.m_torrentHandleMap.erase( it );
                    }
                    else
                    {
                        it++;
                    }
                }

                m_drive.executeOnBackgroundThread( [ filesToRemove=std::move(filesToRemove), this ]
                {
                    removeUnusedFiles( filesToRemove );
                });
            }, false);
        }
    }

    void removeUnusedFiles( const std::set<InfoHash>& filesToRemove )
    {
        DBG_BG_THREAD

        // remove unused files and torrent files from the drive
        for( const auto& hash : filesToRemove )
        {
            std::string filename = hashToFileName( hash );
            std::error_code ec;
            fs::remove( fs::path( m_drive.m_driveFolder ) / filename, ec );
            fs::remove( fs::path( m_drive.m_torrentFolder ) / filename, ec );
        }
        
        // remove unused data from 'fileMap'
        std::erase_if( m_drive.m_torrentHandleMap, [] (const auto& it) { return ! it.second.m_isUsed; } );

        m_drive.executeOnSessionThread( [this]
        {
            if ( m_taskIsInterrupted )
            {
                finishTask();
            }
            else
            {
                startDownloadMissingFiles();
            }
        });
    }
    
    void startDownloadMissingFiles()
    {
        DBG_MAIN_THREAD

        _LOG( "startDownloadMissingFiles: " << m_downloadingLtHandle->id() << " "
                                                                       << m_downloadingLtHandle->info_hashes().v2 );

        _ASSERT( m_sandboxFsTree )

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

        _ASSERT( !m_taskIsInterrupted );

        if ( m_catchingUpFileIt == m_catchingUpFileSet.end())
        {
            m_downloadingLtHandle.reset();

            // it is the end of list
            m_drive.executeOnBackgroundThread( [this]
            {
                modifyDriveInSandbox();
            });
        }
        else
        {
            auto missingFileHash = *m_catchingUpFileIt;
            m_catchingUpFileIt++;

            if ( auto session = m_drive.m_session.lock(); session )
            {
                _ASSERT( m_opinionController.opinionTrafficTx())
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
                                                                   if ( code == download_status::download_complete )
                                                                   {
                                                                       //_ASSERT( fs::exists( m_drive.m_driveFolder / toString( infoHash )))

                                                                       downloadMissingFiles();
                                                                   } else if ( code == download_status::dn_failed )
                                                                   {
                                                                       _LOG_ERR( "? is it possible now?" );
                                                                   }
                                                               },

                                                               missingFileHash,
                                                               *m_opinionController.opinionTrafficTx(),
                                                               0, true, ""
                                                           ),
                                                           m_drive.m_driveFolder.string(),
                                                           (m_drive.m_torrentFolder / toString(missingFileHash)).string(),
                                                           getUploaders(),
                                                           &m_drive.m_driveKey.array(),
                                                           nullptr,
                                                           &m_opinionController.opinionTrafficTx().value().array()
                                                          );
                // save reference into 'torrentHandleMap'
                m_drive.m_torrentHandleMap[missingFileHash] = UseTorrentInfo{*m_downloadingLtHandle, false};

            }
        }
    }

    void modifyDriveInSandbox()
    {
        DBG_BG_THREAD

        getSandboxDriveSizes( m_metaFilesSize, m_sandboxDriveSize );
        m_fsTreeSize = sandboxFsTreeSize();

        m_drive.executeOnSessionThread( [this]() mutable
                                        {
                                            myRootHashIsCalculated();
                                        } );
    }

    // Returns 'true' if 'CatchingUp' should be started
    bool onApprovalTxPublished( const PublishedModificationApprovalTransactionInfo& transaction ) override
    {
        DBG_MAIN_THREAD

        if ( m_taskIsInterrupted )
        {
            return true;
        }

        breakTorrentDownloadAndRunNextTask();
        return true;
    }

    const Hash256& getModificationTransactionHash() override
    {
        return m_request->m_modifyTransactionHash;
    }

    void modifyIsCompleted() override
    {
        _LOG( "catchingIsCompleted" );
		if ( m_drive.m_dbgEventHandler )
		{
			m_drive.m_dbgEventHandler->driveModificationIsCompleted(
					m_drive.m_replicator, m_drive.m_driveKey, m_request->m_modifyTransactionHash, *m_sandboxRootHash);
		}
		UpdateDriveTaskBase::modifyIsCompleted();
    }

    void continueSynchronizingDriveWithSandbox() override
    {
        DBG_BG_THREAD

        try
        {
            _ASSERT( m_sandboxRootHash == m_request->m_rootHash );

            fs::rename( m_drive.m_sandboxFsTreeFile, m_drive.m_fsTreeFile );
            fs::rename( m_drive.m_sandboxFsTreeTorrent, m_drive.m_fsTreeTorrent );

            m_drive.m_serializer.saveRestartValue( getModificationTransactionHash().array(), "approvedModification" );

            auto& torrentHandleMap = m_drive.m_torrentHandleMap;

            // remove unused files and torrent files from the drive
            for ( const auto& it : torrentHandleMap )
            {
                const UseTorrentInfo& info = it.second;
                if ( ! info.m_isUsed )
                {
                    const auto& hash = it.first;
                    std::string filename = hashToFileName( hash );
                    fs::remove( fs::path( m_drive.m_driveFolder ) / filename );
                    fs::remove( fs::path( m_drive.m_torrentFolder ) / filename );
                }
            }

            // Add FsTree torrent to session
            if ( auto session = m_drive.m_session.lock(); session )
            {
                m_sandboxFsTreeLtHandle = session->addTorrentFileToSession( m_drive.m_fsTreeTorrent.string(),
                                                                            m_drive.m_fsTreeTorrent.parent_path().string(),
                                                                            lt::SiriusFlags::peer_is_replicator,
                                                                            &m_drive.m_driveKey.array(),
                                                                            nullptr,
                                                                            nullptr );
            }

            // remove unused data from 'torrentMap'
            std::erase_if( torrentHandleMap, []( const auto& it )
            { return !it.second.m_isUsed; } );

            LOG( "drive is synchronized" );

            m_drive.executeOnSessionThread( [this]
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

        if ( m_taskIsInterrupted )
        {
            finishTask();
            return;
        }

        m_sandboxCalculated = true;

        sendSingleApprovalTransaction( *m_myOpinion );

        startSynchronizingDriveWithSandbox();
    }

    uint64_t getToBeApprovedDownloadSize() override
    {
        return 0;
    }

    void tryBreakTask() override
    {

    }
};

std::unique_ptr<DriveTaskBase> createCatchingUpTask( mobj<CatchingUpRequest>&& request,
                                                     DriveParams& drive,
                                                     ModifyOpinionController& opinionTaskController )
{
    return std::make_unique<CatchingUpTask>( std::move(request), drive, opinionTaskController);
}

}

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

class SynchronizationTaskBase
        : public UpdateDriveTaskBase
{
private:

    std::set<InfoHash> m_catchingUpFileSet;
    std::set<InfoHash>::iterator m_catchingUpFileIt = m_catchingUpFileSet.end();

public:

    SynchronizationTaskBase(
            const DriveTaskType& type,
            DriveParams& drive,
            ModifyOpinionController& opinionTaskController )
            : UpdateDriveTaskBase( type, drive, opinionTaskController )
    {
    }

    void terminate() override
    {
        DBG_MAIN_THREAD

        breakTorrentDownloadAndRunNextTask();
    }

    void run() override
    {
        DBG_MAIN_THREAD

        if ( m_drive.m_lastApprovedModification == getModificationTransactionHash() )
        {
            _ASSERT( m_drive.m_rootHash == getRootHash() );
            finishTask();
            return;
        }

        if ( getRootHash() == m_drive.m_rootHash )
        {
            _LOG( "No need to catch up to " << getRootHash() )

            m_drive.executeOnBackgroundThread( [this]
                                               {
                                                   m_sandboxRootHash = getRootHash();
                                                   m_sandboxRootHash = m_drive.m_rootHash;
                                                   m_sandboxFsTree = std::make_unique<FsTree>();
                                                   m_sandboxFsTree->deserialize( m_drive.m_fsTreeFile.string());
                                                   std::error_code ec;
                                                   fs::remove( m_drive.m_sandboxFsTreeFile, ec );
                                                   fs::copy( m_drive.m_fsTreeFile, m_drive.m_sandboxFsTreeFile );
                                                   fs::remove( m_drive.m_sandboxFsTreeTorrent, ec );
                                                   fs::copy( m_drive.m_fsTreeTorrent, m_drive.m_sandboxFsTreeTorrent );
                                                   modifyDriveInSandbox();
                                               } );
            return;
        }

        _LOG( "started catching up" )

        //
        // Start download fsTree
        //
        if ( auto session = m_drive.m_session.lock(); session )
        {
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
                                    m_fsTreeOrActionListHandle = m_downloadingLtHandle;
                                    m_downloadingLtHandle.reset();
                                    m_drive.executeOnBackgroundThread( [this]
                                                                       {
                                                                           try
                                                                           {
                                                                               m_sandboxFsTree = std::make_unique<FsTree>();
                                                                               m_sandboxFsTree->deserialize(
                                                                                       m_drive.m_sandboxFsTreeFile.string());
                                                                               m_sandboxFsTree->dbgPrint();
                                                                           }
                                                                           catch ( ... )
                                                                           {
                                                                               _LOG_ERR(
                                                                                       "cannot deserialize 'CatchingUpFsTree'" );
                                                                               return;
                                                                           }

                                                                           m_drive.executeOnSessionThread( [this]
                                                                                                           {
                                                                                                               createUnusedFileList();
                                                                                                           } );
                                                                       } );
                                }
                            },
                            getRootHash(),
                            getModificationTransactionHash(),
                            0, true, m_drive.m_sandboxFsTreeFile
                    ),
                    m_drive.m_sandboxRootPath.string(),
                    m_drive.m_sandboxFsTreeTorrent.string(),
                    getUploaders(),
                    &m_drive.m_driveKey.array(),
                    nullptr,
                    &getModificationTransactionHash().array());
        }
    }

protected:

    virtual const Hash256& getRootHash() = 0;

public:

    bool shouldCancelModify( const ModificationCancelRequest& cancelRequest ) override
    {

        DBG_MAIN_THREAD

        // It is a very rare situation:
        // After the initialization we are running the task
        // Without actually need to catch up
        // During the task is beeing finished the cancel is requested
        if ( cancelRequest.m_modifyTransactionHash == m_opinionController.notApprovedModificationId())
        {
            _ASSERT( cancelRequest.m_modifyTransactionHash != getModificationTransactionHash() )
            _ASSERT( m_drive.m_lastApprovedModification == getModificationTransactionHash() )
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

        for ( auto& it : torrentHandleMap )
        {
            it.second.m_isUsed = false;
        }

        markUsedFiles( *m_sandboxFsTree );

        // Prepare set<> for to be removed torrents
        std::set<lt::torrent_handle> toBeRemovedTorrents;

        // Add unused files into set<>
        for ( const auto& it : torrentHandleMap )
        {
            _ASSERT( it.first != Hash256())
            const UseTorrentInfo& info = it.second;
            if ( !info.m_isUsed )
            {
                if ( info.m_ltHandle.is_valid())
                {
                    toBeRemovedTorrents.insert( info.m_ltHandle );
                }
            }
        }

        toBeRemovedTorrents.insert( *m_fsTreeOrActionListHandle );
        m_fsTreeOrActionListHandle.reset();

        // Remove unused torrents
        if ( auto session = m_drive.m_session.lock(); session )
        {
            session->removeTorrentsFromSession( toBeRemovedTorrents, [this]
            {
                std::set<InfoHash> filesToRemove;

                for ( auto it = m_drive.m_torrentHandleMap.begin(); it != m_drive.m_torrentHandleMap.end(); )
                {
                    if ( !it->second.m_isUsed )
                    {
                        filesToRemove.insert( it->first );
                        it = m_drive.m_torrentHandleMap.erase( it );
                    } else
                    {
                        it++;
                    }
                }

                m_drive.executeOnBackgroundThread( [filesToRemove = std::move( filesToRemove ), this]
                                                   {
                                                       removeUnusedFiles( filesToRemove );
                                                   } );
            }, false );
        }
    }

    void removeUnusedFiles( const std::set<InfoHash>& filesToRemove )
    {
        DBG_BG_THREAD

        // remove unused files and torrent files from the drive
        for ( const auto& hash : filesToRemove )
        {
            _ASSERT( hash != Hash256())
            std::string filename = hashToFileName( hash );
            std::error_code ec;
            fs::remove( fs::path( m_drive.m_driveFolder ) / filename, ec );
            fs::remove( fs::path( m_drive.m_torrentFolder ) / filename, ec );
        }

        // remove unused data from 'fileMap'
        std::erase_if( m_drive.m_torrentHandleMap, []( const auto& it )
        { return !it.second.m_isUsed; } );

        m_drive.executeOnSessionThread( [this]
                                        {
                                            if ( m_taskIsInterrupted )
                                            {
                                                finishTask();
                                            } else
                                            {
                                                startDownloadMissingFiles();
                                            }
                                        } );
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

        for ( const auto&[name, child] : folder.childs())
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
                    _ASSERT( hash != Hash256())
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
                                               } );
        } else
        {
            auto missingFileHash = *m_catchingUpFileIt;

            _ASSERT( missingFileHash != Hash256())

            m_catchingUpFileIt++;

            if ( auto session = m_drive.m_session.lock(); session )
            {
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
                                getModificationTransactionHash(),
                                0, true, ""
                        ),
                        m_drive.m_driveFolder.string(),
                        (m_drive.m_torrentFolder / toString( missingFileHash )).string(),
                        getUploaders(),
                        &m_drive.m_driveKey.array(),
                        nullptr,
                        &getModificationTransactionHash().array()
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

    void continueSynchronizingDriveWithSandbox() override
    {
        DBG_BG_THREAD

        try
        {
            _ASSERT( m_sandboxRootHash == getRootHash() );

            fs::rename( m_drive.m_sandboxFsTreeFile, m_drive.m_fsTreeFile );
            fs::rename( m_drive.m_sandboxFsTreeTorrent, m_drive.m_fsTreeTorrent );

            m_drive.m_serializer.saveRestartValue( getModificationTransactionHash().array(), "approvedModification" );

            auto& torrentHandleMap = m_drive.m_torrentHandleMap;

            // remove unused files and torrent files from the drive
            for ( const auto& it : torrentHandleMap )
            {
                const UseTorrentInfo& info = it.second;
                if ( !info.m_isUsed )
                {
                    const auto& hash = it.first;
                    _ASSERT( hash != Hash256())
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
        catch ( const std::exception& ex )
        {
            _LOG_ERR( "exception during completeCatchingUp: " << ex.what());
            finishTask();
        }
    }

    void myOpinionIsCreated() override
    {
        DBG_MAIN_THREAD

        if ( m_taskIsInterrupted )
        {
            finishTask();
            return;
        }

        m_sandboxCalculated = true;

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

}
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

    const mobj<CatchingUpRequest> m_request;

    std::set<InfoHash> m_catchingUpFileSet;
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

        if ( !m_opinionController.opinionTrafficTx())
        {
            m_opinionController.setOpinionTrafficTx( m_request->m_modifyTransactionHash.array() );

            _LOG ("catching up opinion identifier: " << m_request->m_modifyTransactionHash );
        }

        if ( auto session = m_drive.m_session.lock(); session )
        {
            _ASSERT( m_opinionController.opinionTrafficTx())
            m_downloadingLtHandle = session->download( DownloadContext(
                                                               DownloadContext::missing_files,
                                                               std::bind( &CatchingUpTask::catchingUpFsTreeDownloadHandler, this, _1, _2, _3, _4, _5, _6 ),
                                                               m_request->m_rootHash,
                                                               *m_opinionController.opinionTrafficTx(),
                                                               0,
                                                               false,
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

    // Returns 'true' if 'CatchingUp' should be started
    bool onApprovalTxPublished( const PublishedModificationApprovalTransactionInfo& transaction ) override
    {
        DBG_MAIN_THREAD

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
        UpdateDriveTaskBase::modifyIsCompleted();
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

        _LOG( "startDownloadMissingFiles: " << m_downloadingLtHandle->id() << " "
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
                _ASSERT( m_opinionController.opinionTrafficTx())
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
                                                                   *m_opinionController.opinionTrafficTx(),
                                                                   0,
                                                                   false,
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
                _LOG( "rename what:" << m_drive.m_sandboxRootPath / fileName )
                _LOG( "rename to:"   << m_drive.m_driveFolder / fileName )
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

    uint64_t getToBeApprovedDownloadSize() override
    {
        return 0;
    }

    bool isFinishCallable() override
    {
        return true;
    }
};

std::unique_ptr<DriveTaskBase> createCatchingUpTask( mobj<CatchingUpRequest>&& request,
                                                     DriveParams& drive,
                                                     ModifyOpinionController& opinionTaskController )
{
    return std::make_unique<CatchingUpTask>( std::move(request), drive, opinionTaskController);
}

}

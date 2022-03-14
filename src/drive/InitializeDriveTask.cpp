/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "DriveTaskBase.h"

#include <cereal/types/vector.hpp>
#include <cereal/types/optional.hpp>

namespace sirius::drive
{
class InitializeDriveTask : public DriveTaskBase
{

    ModifyOpinionController& m_opinionController;

    std::optional<ApprovalTransactionInfo> m_singleTx;

public:

    InitializeDriveTask( DriveParams& drive,
                         ModifyOpinionController& opinionTaskController)
            : DriveTaskBase( DriveTaskType::DRIVE_INITIALIZATION, drive ),
              m_opinionController( opinionTaskController )
    {}

    void run() override
    {
        m_drive.executeOnBackgroundThread( [this]
                                           {
                                               initialize();
                                           } );
    }

    void terminate() override
    {
        DBG_MAIN_THREAD
    }

    // Returns 'true' if 'CatchingUp' should be started
    bool onApprovalTxPublished( const PublishedModificationApprovalTransactionInfo& transaction ) override
    {
        // We will try to catch up, if we are already on the actual root hash, nothing will happen
        return true;
    }

private:

    void initialize()
    {
        DBG_BG_THREAD

        // Clear m_rootDriveHash
        m_drive.m_rootHash = Hash256();
        _LOG( "m_rootHash=" << m_drive.m_rootHash )

        std::error_code err;

        // Create nonexistent folders
        if ( !fs::exists( m_drive.m_fsTreeFile, err ))
        {
            if ( !fs::exists( m_drive.m_driveFolder, err ))
            {
                fs::create_directories( m_drive.m_driveFolder, err );
            }

            if ( !fs::exists( m_drive.m_torrentFolder, err ))
            {
                fs::create_directories( m_drive.m_torrentFolder, err );
            }
        }

        if ( !fs::exists( m_drive.m_restartRootPath, err ))
        {
            fs::create_directories( m_drive.m_restartRootPath, err );
        }

        // Load FsTree
        if ( fs::exists( m_drive.m_fsTreeFile, err ))
        {
            try
            {
                m_drive.m_fsTree->deserialize( m_drive.m_fsTreeFile );
            }
            catch (const std::exception& ex)
            {
                _LOG_ERR( "initializeDrive: m_fsTree.deserialize exception: " << ex.what())
                fs::remove( m_drive.m_fsTreeFile, err );
            }
        }

        // If FsTree is absent,
        // create it
        if ( !fs::exists( m_drive.m_fsTreeFile, err ))
        {
            fs::create_directories( m_drive.m_fsTreeFile.parent_path(), err );
            m_drive.m_fsTree->name() = "/";
            try
            {
                m_drive.m_fsTree->doSerialize( m_drive.m_fsTreeFile );
            }
            catch (const std::exception& ex)
            {
                _LOG_ERR( "m_fsTree.doSerialize exception:" << ex.what())
            }
        }

        // Calculate torrent and root hash
        m_drive.m_rootHash = createTorrentFile( m_drive.m_fsTreeFile,
                                                m_drive.m_driveKey,
                                                m_drive.m_fsTreeFile.parent_path(),
                                                m_drive.m_fsTreeTorrent );
        
        _LOG( "m_rootHash=" << m_drive.m_rootHash )

        // Add files to session
        addFilesToSession( *m_drive.m_fsTree );

        // Add FsTree to session
        if ( auto session = m_drive.m_session.lock(); session )
        {
            if ( !fs::exists( m_drive.m_fsTreeTorrent, err ))
            {
                //TODO try recovery!
                _LOG_ERR( "disk corrupted: fsTreeTorrent does not exist: " << m_drive.m_fsTreeTorrent )
            }
            m_drive.m_fsTreeLtHandle = session->addTorrentFileToSession( m_drive.m_fsTreeTorrent,
                                                                         m_drive.m_fsTreeTorrent.parent_path(),
                                                                         lt::SiriusFlags::peer_is_replicator,
                                                                         &m_drive.m_driveKey.array(),
                                                                         nullptr,
                                                                         nullptr );
        }

        m_singleTx = loadSingleApprovalTransaction();

        m_opinionController.initialize();

        m_drive.executeOnSessionThread( [this]
                                        {
                                            onInitialized();
                                        } );
    }

    void onInitialized()
    {
        DBG_MAIN_THREAD

        if ( m_singleTx )
        {
            // send single tx info that was be saved
            sendSingleApprovalTransaction( *m_singleTx );
        }

        if ( m_drive.m_dbgEventHandler )
        {
            m_drive.m_dbgEventHandler->driveIsInitialized( m_drive.m_replicator, m_drive.m_driveKey,
                                                           m_drive.m_rootHash );
        }

        _LOG ( "Initialized" )
        finishTask();
    }

    // Recursively marks 'm_toBeRemoved' as false
    //
    void addFilesToSession( const Folder& folder )
    {
        DBG_BG_THREAD

        for ( const auto& child : folder.childs())
        {
            if ( isFolder( child ))
            {
                addFilesToSession( getFolder( child ));
            } else
            {
                auto& hash = getFile( child ).hash();
                std::string fileName = hashToFileName( hash );
                std::error_code err;

                if ( !fs::exists( m_drive.m_driveFolder / fileName, err ))
                {
                    //TODO inform user?
                    _LOG_ERR( "disk corrupted: drive file does not exist: "
                                      << m_drive.m_driveFolder / fileName );
                }

                if ( !fs::exists( m_drive.m_torrentFolder / fileName, err ))
                {
                    //TODO try recovery
                    _LOG_ERR( "disk corrupted: torrent file does not exist: "
                                      << m_drive.m_torrentFolder / fileName )
                }

                if ( auto session = m_drive.m_session.lock(); session )
                {
                    auto ltHandle = session->addTorrentFileToSession( m_drive.m_torrentFolder / fileName,
                                                                      m_drive.m_driveFolder,
                                                                      lt::SiriusFlags::peer_is_replicator,
                                                                      &m_drive.m_driveKey.array(),
                                                                      nullptr,
                                                                      nullptr );
                    m_drive.m_torrentHandleMap.try_emplace( hash, UseTorrentInfo{ltHandle, true} );
                }
            }
        }
    }
};

std::unique_ptr<DriveTaskBase> createDriveInitializationTask( DriveParams& drive,
                                                              ModifyOpinionController& opinionTaskController )
{
    return std::make_unique<InitializeDriveTask>( drive, opinionTaskController );
}

}

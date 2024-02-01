/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "DriveTaskBase.h"
#include "drive/FlatDrive.h"

#include <cereal/types/vector.hpp>
#include <cereal/types/optional.hpp>

#include <fstream>

namespace sirius::drive
{
class InitializeDriveTask : public DriveTaskBase
{

    std::vector<CompletedModification> m_completedModifications;

    ModifyOpinionController& m_opinionController;

    std::optional<ApprovalTransactionInfo> m_singleTx;

    std::vector<ModificationCancelRequest> m_cancelRequests;

    bool m_initialized = false;

public:

    InitializeDriveTask( std::vector<CompletedModification>&& completedModifications,
                         DriveParams& drive,
                         ModifyOpinionController& opinionTaskController)
            : DriveTaskBase( DriveTaskType::DRIVE_INITIALIZATION, drive )
            , m_completedModifications( std::move( completedModifications ) )
            , m_opinionController( opinionTaskController )
    {}

    void run() override
    {
        m_drive.executeOnBackgroundThread( [this]
                                           {
                                               initialize();
                                           } );
    }

    void shutdown() override
    {
        DBG_MAIN_THREAD
    }

    // Returns 'true' if 'CatchingUp' should be started
    bool onApprovalTxPublished( const PublishedModificationApprovalTransactionInfo& transaction ) override
    {
        // We will try to catch up, if we are already on the actual root hash, nothing will happen
        return true;
    }

    void interruptTask( const ModificationCancelRequest& cancelRequest, bool& cancelRequestIsAccepted ) override
    {
        DBG_MAIN_THREAD

        if ( m_initialized )
        {
            if ( m_opinionController.notApprovedModificationId() == cancelRequest.m_modifyTransactionHash.array() )
            {
                cancelRequestIsAccepted = true;
                return;
            }
        }
        else
        {
            m_cancelRequests.push_back(cancelRequest);
        }
        cancelRequestIsAccepted = false;
    }

private:

    void initialize()
    {
        DBG_BG_THREAD

        // Clear m_rootDriveHash
        m_drive.m_rootHash = Hash256();

        try
        {
            // Create nonexistent folders
            if ( !fs::exists( m_drive.m_fsTreeFile ))
            {
                if ( !fs::exists( m_drive.m_driveFolder ))
                {
                    fs::create_directories( m_drive.m_driveFolder );
                }

                if ( !fs::exists( m_drive.m_torrentFolder ))
                {
                    fs::create_directories( m_drive.m_torrentFolder );
                }
            }

            if ( !fs::exists( m_drive.m_restartRootPath ))
            {
                fs::create_directories( m_drive.m_restartRootPath );
            }

            if ( !fs::exists( m_drive.m_emptyFile )) {
                std::ofstream( m_drive.m_emptyFile.c_str());
            }

            if ( !fs::exists( m_drive.m_emptyFileTorrent )) {
                std::ofstream( m_drive.m_emptyFileTorrent.c_str());
            }

            // Load FsTree
            if ( fs::exists( m_drive.m_fsTreeFile ))
            {
                try
                {
                    m_drive.m_fsTree->deserialize( m_drive.m_fsTreeFile );
                    m_drive.updateStreamMap();
                }
                catch (const std::exception& ex)
                {
                    _LOG_ERR( "initializeDrive: m_fsTree.deserialize exception: " << ex.what())
                }
            }

            // If FsTree is absent,
            // create it
            if ( !fs::exists( m_drive.m_fsTreeFile ))
            {
                fs::create_directories( m_drive.m_fsTreeFile.parent_path() );
                m_drive.m_fsTree->name() = "/";
                try
                {
                    m_drive.m_fsTree->doSerialize( m_drive.m_fsTreeFile.string() );
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

            std::array<uint8_t, 32> modificationId{};
            m_drive.m_serializer.loadRestartValue( modificationId, "approvedModification" );
            m_drive.m_lastApprovedModification = modificationId;

            _LOG( "m_lastApprovedModificationId=" << m_drive.m_lastApprovedModification )

            // Add files to session
            addFilesToSession( *m_drive.m_fsTree );

            // Add FsTree to session
            if ( auto session = m_drive.m_session.lock(); session )
            {
                if ( !fs::exists( m_drive.m_fsTreeTorrent ))
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
        }
        catch (const fs::filesystem_error& ex)
        {
            _LOG_ERR( "Drive Initialization Error " << ex.what() << " " << ex.path1() << " " << ex.path2() )
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

        m_initialized = true;

        if ( m_singleTx )
        {
            // send single tx info that was be saved
            sendSingleApprovalTransaction( *m_singleTx );
        }

        bool foundAppropriateCancel = false;

        for ( const auto& cancelRequest: m_cancelRequests)
        {
            if ( cancelRequest.m_modifyTransactionHash.array() == m_opinionController.notApprovedModificationId() )
            {
                SIRIUS_ASSERT( !foundAppropriateCancel )
                foundAppropriateCancel = true;
                _LOG( "Modification Has Been Cancelled During Initialization" );
                m_drive.cancelModifyDrive( std::make_unique<ModificationCancelRequest>(cancelRequest) );
            }
        }

        auto it = std::find_if( m_completedModifications.begin(), m_completedModifications.end(), [this] (const auto& item) {
            return item.m_modificationId.array() == m_opinionController.notApprovedModificationId();
        });

        if ( it != m_completedModifications.end() && it->m_completedModificationStatus == CompletedModification::CompletedModificationStatus::CANCELLED )
        {
            SIRIUS_ASSERT( !foundAppropriateCancel )
            foundAppropriateCancel = true;
            _LOG( "Modification Has Been Cancelled During Offline" );
            m_drive.cancelModifyDrive( std::make_unique<ModificationCancelRequest>( it->m_modificationId ) );
        }

        if ( m_opinionController.approvedModificationId() != m_drive.m_lastApprovedModification )
        {
            // This could happen in a very rare case of "blackout" between calls
            //         m_serializer.saveRestartValue( m_approvedCumulativeUploads, "approvedCumulativeUploads" );
            //     and
            //         m_serializer.saveRestartValue( modificationId.array(), "approvedModification" );
            //
            // SIRIUS_ASSERT( m_opinionController.approvedModificationId() == m_drive.m_lastApprovedModification )
            //
            _LOG( "!!!ERROR!!! m_opinionController.approvedModificationId() != m_drive.m_lastApprovedModification" );
            _LOG( "!!!ERROR!!! m_opinionController.approvedModificationId(): " << m_opinionController.approvedModificationId() );
            _LOG( "!!!ERROR!!! m_drive.m_lastApprovedModification: " << m_drive.m_lastApprovedModification );

            // This is the case if the modification has been interrupted and approved modifications has not been updated
//            m_opinionController.approveCumulativeUploads( m_drive.m_lastApprovedModification, [this] {
//                onApprovedOpinionRestored();
//            });
//            return;
        }
        onApprovedOpinionRestored();
    }

    void onApprovedOpinionRestored()
    {
        DBG_MAIN_THREAD

        if ( m_drive.m_dbgEventHandler )
        {
            m_drive.m_dbgEventHandler->driveIsInitialized( m_drive.m_replicator, m_drive.m_driveKey,
                                                           m_drive.m_rootHash );
        }

        _LOG ( "Initialized" )
        finishTaskAndRunNext();
    }

    void addFilesToSession( const Folder& folder )
    {
        DBG_BG_THREAD

        for ( const auto& [name, child] : folder.childs())
        {
            if ( isFolder( child ))
            {
                addFilesToSession( getFolder( child ));
            } else
            {
                auto& hash = getFile( child ).hash();

                if (hash != Hash256())
                {
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
    }
};

std::unique_ptr<DriveTaskBase> createDriveInitializationTask( std::vector<CompletedModification>&& completedModifications,
                                                              DriveParams& drive,
                                                              ModifyOpinionController& opinionTaskController )
{
    return std::make_unique<InitializeDriveTask>( std::move(completedModifications), drive, opinionTaskController );
}

}

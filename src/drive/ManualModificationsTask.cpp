/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "DriveTaskBase.h"
#include "drive/ManualModificationsRequests.h"
#include "drive/log.h"
#include "drive/Utils.h"

namespace sirius::drive
{

namespace fs = std::filesystem;

class ManualModificationsTask
        : public DriveTaskBase
{

    mobj<InitiateModificationsRequest> m_request;

    std::unique_ptr<FsTree> m_lowerSandboxFsTree;
    std::unique_ptr<FsTree> m_upperSandboxFsTree;

    std::optional<Hash256> m_sandboxRootHash;
    std::optional<lt::torrent_handle> m_sandboxFsTreeHandle;

    // In case of correct implementation we can avoid using shared pointer here
    // But we use them for easier bugs detection
    std::map<uint64_t, std::shared_ptr<std::fstream>> m_openFilesWrite;
    std::map<uint64_t, std::shared_ptr<std::fstream>> m_openFilesRead;

    std::set<InfoHash> m_callManagedHashes;

    uint64_t m_totalFilesOpened = 0;

    bool m_isExecutingQuery = false;

    bool m_taskIsInterrupted = false;

public:

    ManualModificationsTask( mobj<InitiateModificationsRequest>&& request,
                             DriveParams& drive )
            : DriveTaskBase( DriveTaskType::MANUAL_MODIFICATION, drive ), m_request( std::move( request ))
    {}

public:

    void run() override
    {
        DBG_MAIN_THREAD

        _ASSERT( m_drive.m_fsTree )
        m_lowerSandboxFsTree = std::make_unique<FsTree>( *m_drive.m_fsTree );
        m_request->m_callback( InitiateModificationsResponse{} );
    }

    bool initiateSandboxModifications( const InitiateSandboxModificationsRequest& request ) override
    {
        DBG_MAIN_THREAD

        _ASSERT( !m_isExecutingQuery )

        if ( m_taskIsInterrupted )
        {
            return false;
        }

        m_upperSandboxFsTree = std::make_unique<FsTree>( *m_lowerSandboxFsTree );
        request.m_callback( InitiateSandboxModificationsResponse{} );
        return true;
    }

    bool openFile( const OpenFileRequest& request ) override
    {
        DBG_MAIN_THREAD

        _ASSERT( m_upperSandboxFsTree );
        _ASSERT( !m_isExecutingQuery )

        if ( m_taskIsInterrupted )
        {
            return false;
        }

        uint64_t fileId = m_totalFilesOpened;
        m_totalFilesOpened++;

        fs::path p( request.m_path );

        auto pFolder = m_upperSandboxFsTree->getFolderPtr( p.parent_path());

        if ( !pFolder )
        {
            request.m_callback( OpenFileResponse{} );
            return true;
        }

        if ( request.m_mode == OpenFileMode::READ )
        {

            auto it = pFolder->childs().find( p.filename());

            if ( it == pFolder->childs().end())
            {
                request.m_callback( OpenFileResponse{} );
                return true;
            }

            const auto& child = it->second;

            if ( !isFile( child ))
            {
                request.m_callback( OpenFileResponse{} );
                return true;
            }

            auto name = toString( getFile( child ).hash());

            auto absolutePath = m_drive.m_driveFolder / name;

            m_isExecutingQuery = true;
            m_drive.executeOnBackgroundThread( [=, this, mode = request.m_mode, callback = request.m_callback]() mutable
                                               {
                                                   createStream( std::move( absolutePath ), mode, fileId, callback );
                                               } );
        } else
        {
            auto it = pFolder->childs().find( p.filename());

            if ( it != pFolder->childs().end())
            {
                if ( !isFile( it->second ))
                {
                    request.m_callback( OpenFileResponse{} );
                    return true;
                }

                const auto& file = getFile( it->second );

                if ( !m_callManagedHashes.contains( file.hash()))
                {
                    // This file on the disk has not been created during this call so we can not modify it
                    m_upperSandboxFsTree->removeFlat( p, []( const auto& )
                    {} );
                    auto temporaryHash = m_upperSandboxFsTree->addModifiableFile( p.parent_path(), p.filename());
                    _ASSERT(temporaryHash)
                    m_callManagedHashes.insert(*temporaryHash);
                }
            } else
            {
                m_upperSandboxFsTree->addModifiableFile( p.parent_path(), p.filename());
                auto temporaryHash = m_upperSandboxFsTree->addModifiableFile( p.parent_path(), p.filename());
                _ASSERT(temporaryHash)
                m_callManagedHashes.insert(*temporaryHash);
            }

            it = pFolder->childs().find( p.filename());

            const auto& child = it->second;

            auto name = toString( getFile( child ).hash());

            auto absolutePath = m_drive.m_driveFolder / name;

            m_isExecutingQuery = true;
            m_drive.executeOnBackgroundThread( [=, this, mode = request.m_mode, callback = request.m_callback]() mutable
                                               {
                                                   createStream( std::move( absolutePath ), mode, fileId, callback );
                                               } );
        }

        return true;
    }

private:

    void createStream( std::string&& path, OpenFileMode mode, uint64_t fileId,
                       const std::function<void( std::optional<OpenFileResponse> )>& callback )
    {
        DBG_BG_THREAD

        std::ios_base::openmode m = std::ios_base::binary;

        if ( mode == OpenFileMode::READ )
        {
            m |= std::ios_base::in;
        } else
        {
            m |= std::ios_base::out;
        }

        // TODO We use shared pointer here because can not pass move-only objects below
        auto stream = std::make_shared<std::fstream>();

        try
        {
            stream->open( path, m );
        }
        catch ( ... )
        {
            _LOG( "Failed to open stream " << path );
        }

        m_drive.executeOnSessionThread( [=, this]() mutable
                                        {
                                            onFileOpened( std::move( stream ), mode, fileId, callback );
                                        } );
    }

    void onFileOpened( std::shared_ptr<std::fstream>&& stream, OpenFileMode mode, uint64_t fileId,
                       const std::function<void( std::optional<OpenFileResponse> )>& callback )
    {

        DBG_MAIN_THREAD

        _ASSERT( !m_openFilesRead.contains( fileId ))
        _ASSERT( !m_openFilesWrite.contains( fileId ))
        _ASSERT( m_isExecutingQuery )

        m_isExecutingQuery = false;

        if ( m_taskIsInterrupted )
        {
            callback( {} );
            finishTask();
            return;
        }

        if ( !stream->is_open())
        {
            callback( OpenFileResponse{} );
            return;
        }

        if ( mode == OpenFileMode::READ )
        {
            m_openFilesRead[fileId] = std::move( stream );
        } else
        {
            m_openFilesWrite[fileId] = std::move( stream );
        }

        callback( OpenFileResponse{fileId} );
    }

public:

    bool readFile( const ReadFileRequest& request ) override
    {

        DBG_MAIN_THREAD

        _ASSERT( !m_isExecutingQuery )

        if ( m_taskIsInterrupted )
        {
            return false;
        }

        auto it = m_openFilesRead.find( request.m_fileId );

        if ( it == m_openFilesRead.end())
        {
            request.m_callback( ReadFileResponse{std::make_optional<std::vector<uint8_t>>()} );
            return true;
        }

        m_isExecutingQuery = true;

        auto stream = it->second;

        m_drive.executeOnBackgroundThread(
                [stream, this, bytes = request.m_bytes, callback = std::move( request.m_callback )]
                {
                    readStream( stream, bytes, callback );
                } );

        return true;
    }

private:

    void readStream( const std::weak_ptr<std::fstream>& weakStream, uint64_t bytes,
                     const std::function<void( std::optional<ReadFileResponse> )>& callback )
    {
        DBG_BG_THREAD

        auto stream = weakStream.lock();

        _ASSERT( stream );

        std::vector<uint8_t> buffer( bytes, 0 );
        stream->read( reinterpret_cast<char*>(buffer.data()), buffer.size());
        auto read = stream->gcount();
        buffer.resize( read );

        _ASSERT( stream->good());

        m_drive.executeOnSessionThread( [=, this]() mutable
                                        {
                                            onReadFile( std::move( buffer ), callback );
                                        } );
    }

    void
    onReadFile( std::vector<uint8_t>&& bytes, const std::function<void( std::optional<ReadFileResponse> )>& callback )
    {

        DBG_MAIN_THREAD

        _ASSERT( m_isExecutingQuery )

        m_isExecutingQuery = false;

        if ( m_taskIsInterrupted )
        {
            callback( {} );
            finishTask();
            return;
        }

        callback( ReadFileResponse{bytes} );
    }

public:

    bool writeFile( const WriteFileRequest& request ) override
    {

        DBG_MAIN_THREAD

        _ASSERT( !m_isExecutingQuery )

        if ( m_taskIsInterrupted )
        {
            return false;
        }

        auto it = m_openFilesWrite.find( request.m_fileId );

        if ( it == m_openFilesWrite.end())
        {
            request.m_callback( WriteFileResponse{false} );
            return true;
        }

        m_isExecutingQuery = true;

        auto stream = it->second;

        m_drive.executeOnBackgroundThread(
                [stream, this, buffer = request.m_buffer, callback = request.m_callback]() mutable
                {
                    writeStream( stream, std::move( buffer ), callback );
                } );

        return true;
    }

private:

    void writeStream( const std::weak_ptr<std::fstream>& weakStream, std::vector<uint8_t>&& buffer,
                      const std::function<void( std::optional<WriteFileResponse> )>& callback )
    {
        DBG_BG_THREAD

        _LOG( "In Stream" );

        auto stream = weakStream.lock();

        _ASSERT( stream );

        stream->write( reinterpret_cast<char*>(buffer.data()), buffer.size());

        _ASSERT( stream->good())

        m_drive.executeOnSessionThread( [=, this]() mutable
                                        {
                                            onFileWritten( true, callback );
                                        } );
    }

    void onFileWritten( bool success, const std::function<void( std::optional<WriteFileResponse> )>& callback )
    {
        DBG_MAIN_THREAD

        _ASSERT( m_isExecutingQuery )

        m_isExecutingQuery = false;

        _LOG( "On File Written" );

        if ( m_taskIsInterrupted )
        {
            callback( {} );
            finishTask();
            return;
        }

        callback( WriteFileResponse{success} );
    }

public:

    bool flush( const FlushRequest& request ) override
    {
        DBG_MAIN_THREAD

        _ASSERT( !m_isExecutingQuery )

        if ( m_taskIsInterrupted )
        {
            return false;
        }

        auto it = m_openFilesWrite.find( request.m_fileId );

        if ( it == m_openFilesWrite.end())
        {
            request.m_callback( FlushResponse{false} );
            return true;
        }

        auto stream = it->second;

        m_isExecutingQuery = true;
        m_drive.executeOnSessionThread( [=, this, callback = request.m_callback]
                                        {
                                            flushStream( stream, callback );
                                        } );

        return true;
    }

private:

    void
    flushStream( const std::weak_ptr<std::fstream>& weakStream,
                 const std::function<void( std::optional<FlushResponse> )>& callback )
    {

        DBG_BG_THREAD

        auto stream = weakStream.lock();

        _ASSERT( stream );

        stream->flush();

        _ASSERT( stream->good())

        m_drive.executeOnSessionThread( [=, this]
                                        {
                                            onFlushed( true, callback );
                                        } );
    }

    void onFlushed( bool success, const std::function<void( std::optional<FlushResponse> )>& callback )
    {
        DBG_MAIN_THREAD

        _ASSERT( m_isExecutingQuery )

        m_isExecutingQuery = false;

        if ( m_taskIsInterrupted )
        {
            callback( {} );
            finishTask();
            return;
        }

        callback( FlushResponse{success} );
    }

public:

    bool closeFile( const CloseFileRequest& request ) override
    {
        DBG_MAIN_THREAD

        _ASSERT( !m_isExecutingQuery )

        if ( m_taskIsInterrupted )
        {
            return false;
        }

        auto readIt = m_openFilesRead.find( request.m_fileId );

        if ( readIt != m_openFilesRead.end())
        {
            auto stream = readIt->second;
            m_isExecutingQuery = true;
            m_drive.executeOnBackgroundThread( [=, this, fileId = request.m_fileId, callback = request.m_callback]
                                               {
                                                   closeStream( stream, fileId, callback );
                                               } );
            return true;
        }

        auto writeIt = m_openFilesWrite.find( request.m_fileId );

        if ( writeIt != m_openFilesWrite.end())
        {
            auto stream = writeIt->second;
            m_isExecutingQuery = true;
            m_drive.executeOnBackgroundThread( [=, this, fileId = request.m_fileId, callback = request.m_callback]
                                               {
                                                   closeStream( stream, fileId, callback );
                                               } );
            return true;
        }

        request.m_callback( CloseFileResponse{false} );
        return true;
    }

private:

    void closeStream( const std::weak_ptr<std::fstream>& weakStream, uint64_t fileId,
                      const std::function<void( std::optional<CloseFileResponse> )>& callback )
    {

        DBG_BG_THREAD

        auto stream = weakStream.lock();

        _ASSERT( stream );

        try
        {
            stream->close();
        }
        catch ( ... )
        {
            _LOG_WARN( "Failed to close stream" );
        }

        m_drive.executeOnSessionThread( [=, this]
                                        {
                                            onFileClosed( fileId, true, callback );
                                        } );
    }

    void onFileClosed( uint64_t fileId, bool success,
                       const std::function<void( std::optional<CloseFileResponse> )>& callback )
    {
        DBG_MAIN_THREAD

        _ASSERT( m_isExecutingQuery )

        m_isExecutingQuery = false;

        if ( m_taskIsInterrupted )
        {
            callback( {} );
            finishTask();
            return;
        }

        auto readIt = m_openFilesRead.find( fileId );

        if ( readIt != m_openFilesRead.end())
        {
            m_openFilesRead.erase( readIt );
            callback( CloseFileResponse{success} );
            return;
        }

        auto writeIt = m_openFilesWrite.find( fileId );

        if ( writeIt != m_openFilesWrite.end())
        {
            m_openFilesWrite.erase( writeIt );
            callback( CloseFileResponse{success} );
            return;
        }

        _LOG_ERR( "Close nonexisting stream" );
    }

public:

    bool applySandboxModifications( const ApplySandboxModificationsRequest& request ) override
    {
        DBG_MAIN_THREAD

        _ASSERT( !m_isExecutingQuery )

        if ( m_taskIsInterrupted )
        {
            return false;
        }

        _ASSERT( m_upperSandboxFsTree )

        if ( request.m_success )
        {
            m_isExecutingQuery = true;
            m_drive.executeOnBackgroundThread( [this, callback = request.m_callback]
                                               {
                                                   if ( validateSandboxState())
                                                   {
                                                       acceptUpperSandboxModifications( callback );
                                                   } else
                                                   {
                                                       discardUpperSandboxModifications( callback );
                                                   }
                                               } );
        } else
        {
            m_isExecutingQuery = true;
            m_drive.executeOnBackgroundThread( [this, callback = request.m_callback]
                                               {
                                                   discardUpperSandboxModifications( callback );
                                               } );
        }

        return true;
    }

private:

    void discardUpperSandboxModifications(
            const std::function<void( std::optional<ApplySandboxModificationsResponse> )>& callback )
    {
        DBG_BG_THREAD

        for ( const auto& hash: m_callManagedHashes )
        {
            std::error_code ec;
            fs::remove( m_drive.m_driveFolder / toString( hash ), ec );

            if ( ec )
            {
                _LOG_ERR( "Error during removing file: " << ec.message());
            }
        }

        m_drive.executeOnSessionThread( [=, this]
                                        {
                                            onAppliedSandboxModifications( false, callback );
                                        } );
    }

    bool validateSandboxState()
    {
        DBG_BG_THREAD

        auto size = m_upperSandboxFsTree->evaluateSizes( m_drive.m_driveFolder, m_drive.m_torrentFolder );

        // TODO consider fs tree size too
        bool valid = true;

        if ( size >= m_drive.m_maxSize )
        {
            valid = false;
        }

        return valid;
    }

    void acceptUpperSandboxModifications(
            const std::function<void( std::optional<ApplySandboxModificationsResponse> )>& callback )
    {
        DBG_BG_THREAD

        std::set<InfoHash> upperUniqueFiles;
        m_upperSandboxFsTree->getUniqueFiles( upperUniqueFiles );

        std::set<InfoHash> lowerUniqueFiles;
        m_lowerSandboxFsTree->getUniqueFiles( lowerUniqueFiles );

        for ( const auto& file: lowerUniqueFiles )
        {
            if ( !upperUniqueFiles.contains( file ) && !m_drive.m_torrentHandleMap.contains( file ))
            {
                std::error_code ec;
                fs::remove( m_drive.m_driveFolder / toString( file ), ec );

                if ( ec )
                {
                    _LOG_ERR( "Error during removing file: " << ec.message());
                }
            }
        }

        m_drive.executeOnSessionThread( [=, this]
                                        {
                                            onAppliedSandboxModifications( true, callback );
                                        } );
    }

    void onAppliedSandboxModifications( bool success,
                                        const std::function<void(
                                                std::optional<ApplySandboxModificationsResponse> )>& callback )
    {

        DBG_MAIN_THREAD

        _ASSERT( m_isExecutingQuery )

        m_isExecutingQuery = false;

        if ( m_taskIsInterrupted )
        {
            callback( {} );
            finishTask();
            return;
        }

        if ( success )
        {
            m_lowerSandboxFsTree = std::move( m_upperSandboxFsTree );
        } else
        {
            m_upperSandboxFsTree.reset();
            m_callManagedHashes.clear();
        }

        callback( ApplySandboxModificationsResponse{success, 0, 0} );
    }

public:

    bool evaluateStorageHash( const EvaluateStorageHashRequest& request ) override
    {
        DBG_MAIN_THREAD

        _ASSERT( !m_isExecutingQuery )

        if ( m_taskIsInterrupted )
        {
            return false;
        }

        _ASSERT( !m_upperSandboxFsTree )

        m_isExecutingQuery = true;
        m_drive.executeOnBackgroundThread( [this, callback = request.m_callback]
                                           {
                                               computeSandboxHash( callback );
                                           } );

        return true;
    }

private:

    void computeSandboxHash( const std::function<void( std::optional<EvaluateStorageHashResponse> )>& callback )
    {
        DBG_BG_THREAD

        m_lowerSandboxFsTree->mapFiles( [this]( File& file )
                                        {
                                            if ( file.isModifiable())
                                            {
                                                auto filePath = m_drive.m_driveFolder / toString( file.hash());
                                                auto torrentPath = m_drive.m_torrentFolder / toString( file.hash());
                                                auto hash = createTorrentFile( filePath,
                                                                               m_drive.m_driveKey,
                                                                               filePath.parent_path(),
                                                                               torrentPath );

                                                try
                                                {
                                                    if ( m_drive.m_torrentHandleMap.contains( hash ))
                                                    {
                                                        fs::remove( filePath );
                                                        fs::remove( torrentPath );
                                                    } else
                                                    {
                                                        fs::rename( filePath, m_drive.m_driveFolder / toString( hash ));
                                                        fs::rename( torrentPath,
                                                                    m_drive.m_torrentFolder / toString( hash ));
                                                    }
                                                    file.setHash( hash );
                                                    file.setSize(
                                                            fs::file_size( m_drive.m_driveFolder / toString( hash )));
                                                    file.setIsModifiable( false );
                                                }
                                                catch ( const std::filesystem::filesystem_error& er )
                                                {
                                                    _LOG_ERR( "Filesystem error has occurred " << er.what() << " "
                                                                                               << er.path1() << " "
                                                                                               << er.path2());
                                                }
                                            }
                                        } );

        m_lowerSandboxFsTree->doSerialize( m_drive.m_sandboxFsTreeFile.string());

        m_sandboxRootHash = createTorrentFile( m_drive.m_sandboxFsTreeFile.string(),
                                               m_drive.m_driveKey,
                                               m_drive.m_sandboxRootPath.string(),
                                               m_drive.m_sandboxFsTreeTorrent.string());

        m_drive.executeOnSessionThread( [=, this]
                                        {
                                            onStorageHashEvaluated( *m_sandboxRootHash, callback );
                                        } );
    }

    void onStorageHashEvaluated( const InfoHash& storageHash,
                                 const std::function<void( std::optional<EvaluateStorageHashResponse> )>& callback )
    {

        DBG_MAIN_THREAD

        _ASSERT( m_isExecutingQuery )

        m_isExecutingQuery = false;

        if ( m_taskIsInterrupted )
        {
            callback( {} );
            finishTask();
            return;
        }

        callback( EvaluateStorageHashResponse{storageHash, 0, 0, 0} );
    }

public:

    bool applyStorageModifications( const ApplyStorageModificationsRequest& request ) override
    {
        DBG_MAIN_THREAD

        _ASSERT( !m_isExecutingQuery )

        if ( m_taskIsInterrupted )
        {
            return false;
        }

        _ASSERT( !m_upperSandboxFsTree )

        if ( request.m_success )
        {
            m_drive.executeOnBackgroundThread( [this, callback = request.m_callback]
                                               {
                                                   addNewTorrentsToSession( callback );
                                               } );
        } else
        {
            request.m_callback( ApplyStorageModificationsResponse{} );
            finishTask();
        }

        return true;
    }

private:

    void
    addNewTorrentsToSession( const std::function<void( std::optional<ApplyStorageModificationsResponse> )>& callback )
    {
        DBG_BG_THREAD

        std::set<InfoHash> uniqueFiles;
        m_lowerSandboxFsTree->getUniqueFiles( uniqueFiles );
        for ( const auto& file: uniqueFiles )
        {
            auto it = m_drive.m_torrentHandleMap.find( file );

            if ( it == m_drive.m_torrentHandleMap.end())
            {
                auto session = m_drive.m_session.lock();
                if ( session )
                {
                    std::string fileName = toString( file );

                    m_drive.m_torrentHandleMap[file].m_ltHandle = session->addTorrentFileToSession(
                            (m_drive.m_torrentFolder / fileName).string(),
                            m_drive.m_driveFolder.string(),
                            lt::SiriusFlags::peer_is_replicator,
                            &m_drive.m_driveKey.array(),
                            nullptr,
                            nullptr );
                }
            }
        }

        m_drive.executeOnSessionThread( [=, this]
                                        {
                                            onNewTorrentAdded( callback );
                                        } );
    }

    void onNewTorrentAdded( const std::function<void( std::optional<ApplyStorageModificationsResponse> )>& callback )
    {
        DBG_MAIN_THREAD

        markUsedFiles( *m_lowerSandboxFsTree );

        // Prepare set<> for to be removed torrents
        std::set<lt::torrent_handle> toBeRemovedTorrents;

        // Add unused files into set<>
        for ( const auto& it : m_drive.m_torrentHandleMap )
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
            _LOG( "toBeRemovedTorrents.size()=" << toBeRemovedTorrents.size())
            session->removeTorrentsFromSession( toBeRemovedTorrents, [=, this]
            {
                m_drive.executeOnBackgroundThread( [=, this]
                                                   {
                                                       onUnusedTorrentsDeleted( callback );
                                                   } );
            }, false );
        }
    }

    void
    onUnusedTorrentsDeleted( const std::function<void( std::optional<ApplyStorageModificationsResponse> )>& callback )
    {
        DBG_BG_THREAD

        try
        {
            fs::rename( m_drive.m_sandboxFsTreeFile, m_drive.m_fsTreeFile );
            fs::rename( m_drive.m_sandboxFsTreeTorrent, m_drive.m_fsTreeTorrent );

            m_drive.m_serializer.saveRestartValue( m_request->m_modificationIdentifier, "approvedModification" );

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

            // Add FsTree torrent to session
            if ( auto session = m_drive.m_session.lock(); session )
            {
                m_sandboxFsTreeHandle = session->addTorrentFileToSession( m_drive.m_fsTreeTorrent.string(),
                                                                          m_drive.m_fsTreeTorrent.parent_path().string(),
                                                                          lt::SiriusFlags::peer_is_replicator,
                                                                          &m_drive.m_driveKey.array(),
                                                                          nullptr,
                                                                          nullptr );
            }

            // remove unused data from 'torrentMap'
            std::erase_if( torrentHandleMap, []( const auto& it )
            { return !it.second.m_isUsed; } );

            m_drive.executeOnSessionThread( [=, this]
                                            {
                                                onAppliedStorageModifications( callback );
                                            } );
        }
        catch ( const std::exception& ex )
        {
            _LOG_ERR( "Exception during unused file delition: " << ex.what());
        }
    }

    void onAppliedStorageModifications(
            const std::function<void( std::optional<ApplyStorageModificationsResponse> )>& callback )
    {
        DBG_MAIN_THREAD

        _ASSERT( m_sandboxRootHash )
        _ASSERT( m_sandboxFsTreeHandle )

        m_drive.m_fsTree = std::move( m_lowerSandboxFsTree );
        m_drive.m_rootHash = *m_sandboxRootHash;
        m_drive.m_fsTreeLtHandle = *m_sandboxFsTreeHandle;
        m_drive.m_lastApprovedModification = m_request->m_modificationIdentifier;

        callback( ApplyStorageModificationsResponse{} );

        finishTask();
    }

public:

    void terminate() override
    {
        DBG_MAIN_THREAD

        if ( m_taskIsInterrupted )
        {
            return;
        }

        m_taskIsInterrupted = true;

        if ( !m_isExecutingQuery )
        {
            finishTask();
        }
    }

    bool onApprovalTxPublished( const PublishedModificationApprovalTransactionInfo& transaction ) override
    {
        DBG_MAIN_THREAD

        terminate();

        return true;
    }

    void onDriveClose( const DriveClosureRequest& closureRequest ) override
    {
        DBG_MAIN_THREAD

        terminate();
    }

    bool manualSynchronize( const SynchronizationRequest& request ) override
    {
        DBG_MAIN_THREAD

        terminate();

        return true;
    }

};

std::unique_ptr<DriveTaskBase> createManualModificationsTask( mobj<InitiateModificationsRequest>&& request,
                                                              DriveParams& drive )
{
    return std::make_unique<ManualModificationsTask>( std::move( request ), drive );
}

}
/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "DriveTaskBase.h"
#include "drive/ManualModificationsRequests.h"
#include "drive/log.h"
#include "drive/Utils.h"
#include <stack>
#include "FolderIterator.h"

namespace sirius::drive
{

struct OpenFile
{

    OpenFile( std::shared_ptr<std::fstream> stream,
              std::shared_ptr<FileStatisticsNode> statisticsNode )
            : m_stream( std::move( stream ))
            , m_statisticsNode( std::move( statisticsNode ))
    {
        m_statisticsNode->addBlock();
    }

    OpenFile( const OpenFile& ) = delete;

    OpenFile( OpenFile&& ) = default;

    ~OpenFile()
    {
        if ( m_statisticsNode )
        {
            m_statisticsNode->removeBlock();
        }
    }

    std::shared_ptr<std::fstream> m_stream;
    std::shared_ptr<FileStatisticsNode> m_statisticsNode;
};

namespace fs = std::filesystem;

class ManualModificationsTask
        : public DriveTaskBase
{

    mobj<InitiateModificationsRequest> m_request;
    ModifyOpinionController& m_opinionTaskController;

    uint64_t m_upperSandboxFilesSize = 0;
    uint64_t m_lowerSandboxFilesSize = 0;

    std::unique_ptr<FsTree> m_lowerSandboxFsTree;
    std::unique_ptr<FsTree> m_upperSandboxFsTree;

    std::optional<Hash256> m_sandboxRootHash;
    std::optional<lt::torrent_handle> m_sandboxFsTreeHandle;

    std::map<uint64_t, OpenFile> m_openFilesWrite;
    std::map<uint64_t, OpenFile> m_openFilesRead;
    std::map<uint64_t, FolderIterator> m_folderIterators;

    std::set<InfoHash> m_callManagedHashes;

    uint64_t m_totalFilesOpened = 0;
    uint64_t m_totalFolderIteratorsCreated = 0;

    bool m_isExecutingQuery = false;

    bool m_taskIsInterrupted = false;
    bool m_taskIsFinished = false;

    uint64_t m_maxOpenedStreams = 5;

public:

    ManualModificationsTask( mobj<InitiateModificationsRequest>&& request,
                             DriveParams& drive,
                              ModifyOpinionController& opinionTaskController )
            : DriveTaskBase( DriveTaskType::MANUAL_MODIFICATION, drive ), m_request( std::move( request )), m_opinionTaskController(opinionTaskController)
    {}

public:

    void run() override
    {
        DBG_MAIN_THREAD

        SIRIUS_ASSERT ( m_drive.m_fsTree )
        m_lowerSandboxFsTree = std::make_unique<FsTree>( *m_drive.m_fsTree );
        m_request->m_callback( InitiateModificationsResponse{} );
    }

    bool initiateSandboxModifications( const InitiateSandboxModificationsRequest& request ) override
    {
        DBG_MAIN_THREAD

        SIRIUS_ASSERT ( !m_isExecutingQuery )

        if ( m_taskIsInterrupted )
        {
            return false;
        }

        m_upperSandboxFsTree = std::make_unique<FsTree>( *m_lowerSandboxFsTree );
        m_upperSandboxFsTree->initializeStatistics();
        m_upperSandboxFilesSize = m_lowerSandboxFilesSize;

        for (const auto& serviceFolder: request.m_serviceFolders) {
            m_upperSandboxFsTree->addFolder(serviceFolder);
        }

        request.m_callback( InitiateSandboxModificationsResponse{} );
        return true;
    }

    bool openFile( const OpenFileRequest& request ) override
    {
        DBG_MAIN_THREAD

        SIRIUS_ASSERT ( m_upperSandboxFsTree )
        SIRIUS_ASSERT ( !m_isExecutingQuery )

        if ( m_taskIsInterrupted )
        {
            return false;
        }

        if ( m_openFilesRead.size() + m_openFilesWrite.size() >= m_maxOpenedStreams )
        {
            request.m_callback( OpenFileResponse{} );
        }

        uint64_t fileId = m_totalFilesOpened;
        m_totalFilesOpened++;

        fs::path p( request.m_path );

        auto pFolder = m_upperSandboxFsTree->getFolderPtr(p.parent_path().string());

        if ( !pFolder )
        {
            request.m_callback( OpenFileResponse{} );
            return true;
        }

        if ( !checkUnlock( request.m_path ))
        {
            request.m_callback( OpenFileResponse{} );
            return true;
        }

        if ( request.m_mode == OpenFileMode::READ )
        {

            auto it = pFolder->childs().find( p.filename().string() );

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

            auto absolutePath = m_drive.m_driveFolder.string() + "/" + name;

            m_isExecutingQuery = true;
            m_drive.executeOnBackgroundThread(
                    [=, this, mode = request.m_mode, callback = request.m_callback, path = request.m_path]() mutable
                    {
                        createStream( std::move( absolutePath ), mode, fileId, path, callback );
                    } );
        } else
        {
            auto it = pFolder->childs().find( p.filename().string() );

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
                    m_upperSandboxFsTree->removeFlat( p.string(), []( const auto& )
                    {} );
                    auto temporaryHash = m_upperSandboxFsTree->addModifiableFile( p.parent_path().string(), p.filename().string());
                    SIRIUS_ASSERT ( temporaryHash )
                    m_callManagedHashes.insert( *temporaryHash );
                }
            } else
            {
                m_upperSandboxFsTree->addModifiableFile( p.parent_path().string(), p.filename().string());
                auto temporaryHash = m_upperSandboxFsTree->addModifiableFile( p.parent_path().string(), p.filename().string());
                SIRIUS_ASSERT ( temporaryHash )
                m_callManagedHashes.insert( *temporaryHash );
            }

            it = pFolder->childs().find( p.filename().string() );

            const auto& child = it->second;

            auto name = toString( getFile( child ).hash());

            auto absolutePath = m_drive.m_driveFolder.string() + "/" + name;

            m_isExecutingQuery = true;
            m_drive.executeOnBackgroundThread(
                    [=, this, mode = request.m_mode, callback = request.m_callback, path = request.m_path]() mutable
                    {
                        createStream( std::move( absolutePath ), mode, fileId, path, callback );
                    } );
        }

        return true;
    }

private:

    void createStream( std::string&& path,
                       OpenFileMode mode,
                       uint64_t fileId,
                       const std::string& fsPath,
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

        uint64_t clearedFileSize = 0;
        if ( mode == OpenFileMode::WRITE && fs::exists( path ))
        {
            // In the case when such a file exists,
            // it will be overwritten (= old content is removed)
            std::error_code ec;
            clearedFileSize += fs::file_size( path, ec );
        }

        auto stream = std::make_shared<std::fstream>();

        try
        {
            stream->open( path, m );
        }
        catch ( ... )
        {
            _LOG( "Failed to open stream " << path )
        }

        m_drive.executeOnSessionThread( [=, this]() mutable
                                        {
                                            onFileOpened( std::move( stream ), mode, fileId, fsPath, clearedFileSize,
                                                          callback );
                                        } );
    }

    void onFileOpened( std::shared_ptr<std::fstream>&& stream,
                       OpenFileMode mode,
                       uint64_t fileId,
                       const std::string& fsPath,
                       uint64_t clearedFileSize,
                       const std::function<void( std::optional<OpenFileResponse> )>& callback )
    {

        DBG_MAIN_THREAD

        SIRIUS_ASSERT ( !m_openFilesRead.contains( fileId ))
        SIRIUS_ASSERT ( !m_openFilesWrite.contains( fileId ))
        SIRIUS_ASSERT ( m_isExecutingQuery )

        m_isExecutingQuery = false;

        if ( m_taskIsInterrupted )
        {
            callback( {} );
            finishManualTask();
            return;
        }

        SIRIUS_ASSERT ( clearedFileSize <= m_upperSandboxFilesSize )
        m_upperSandboxFilesSize -= clearedFileSize;

        if ( !stream->is_open())
        {
            callback( OpenFileResponse{} );
            return;
        }

        auto* child = m_upperSandboxFsTree->getEntryPtr( fsPath );

        SIRIUS_ASSERT ( child != nullptr )
        SIRIUS_ASSERT ( isFile( *child ))

        auto statisticsNode = getFile( *child ).statisticsNode();

        OpenFile file( stream, statisticsNode );

        if ( mode == OpenFileMode::READ )
        {
            m_openFilesRead.emplace( fileId, std::move( file ));
        } else
        {
            m_openFilesWrite.emplace( fileId, std::move( file ));
        }

        callback( OpenFileResponse{fileId} );
    }

public:

    bool readFile( const ReadFileRequest& request ) override
    {

        DBG_MAIN_THREAD

        SIRIUS_ASSERT ( m_upperSandboxFsTree )
        SIRIUS_ASSERT ( !m_isExecutingQuery )

        if ( m_taskIsInterrupted )
        {
            return false;
        }

        auto it = m_openFilesRead.find( request.m_fileId );

        if ( it == m_openFilesRead.end())
        {
            request.m_callback( ReadFileResponse{} );
            return true;
        }

        m_isExecutingQuery = true;

        auto stream = it->second.m_stream;

        m_drive.executeOnBackgroundThread(
                [stream, this, bytes = request.m_bytes, callback = request.m_callback]
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

        SIRIUS_ASSERT ( stream )

        std::vector<uint8_t> buffer( bytes, 0 );
        stream->read( reinterpret_cast<char*>(buffer.data()), buffer.size());
        auto read = stream->gcount();
        buffer.resize( read );

        SIRIUS_ASSERT ( stream->good() || stream->eof())

        m_drive.executeOnSessionThread( [=, this]() mutable
                                        {
                                            onReadFile( std::move( buffer ), callback );
                                        } );
    }

    void
    onReadFile( std::vector<uint8_t>&& bytes,
                const std::function<void( std::optional<ReadFileResponse> )>& callback )
    {

        DBG_MAIN_THREAD

        SIRIUS_ASSERT ( m_isExecutingQuery )

        m_isExecutingQuery = false;

        if ( m_taskIsInterrupted )
        {
            callback( {} );
            finishManualTask();
            return;
        }

        callback( ReadFileResponse{bytes} );
    }

public:

    bool writeFile( const WriteFileRequest& request ) override
    {

        DBG_MAIN_THREAD

        SIRIUS_ASSERT ( m_upperSandboxFsTree )
        SIRIUS_ASSERT ( !m_isExecutingQuery )

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

        if ( m_upperSandboxFilesSize + request.m_buffer.size() > m_drive.m_maxSize )
        {
            request.m_callback( WriteFileResponse{false} );
            return true;
        }

        m_upperSandboxFilesSize += request.m_buffer.size();

        m_isExecutingQuery = true;

        auto stream = it->second.m_stream;

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

        _LOG( "In Stream" )

        auto stream = weakStream.lock();

        SIRIUS_ASSERT ( stream )

        stream->write( reinterpret_cast<char*>(buffer.data()), buffer.size());

        SIRIUS_ASSERT ( stream->good())

        m_drive.executeOnSessionThread( [=, this]() mutable
                                        {
                                            onFileWritten( true, callback );
                                        } );
    }

    void onFileWritten( bool success, const std::function<void( std::optional<WriteFileResponse> )>& callback )
    {
        DBG_MAIN_THREAD

        SIRIUS_ASSERT ( m_isExecutingQuery )

        m_isExecutingQuery = false;

        _LOG( "On File Written" )

        if ( m_taskIsInterrupted )
        {
            callback( {} );
            finishManualTask();
            return;
        }

        callback( WriteFileResponse{success} );
    }

public:

    bool flush( const FlushRequest& request ) override
    {
        DBG_MAIN_THREAD

        SIRIUS_ASSERT ( m_upperSandboxFsTree )
        SIRIUS_ASSERT ( !m_isExecutingQuery )

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

        auto stream = it->second.m_stream;

        m_isExecutingQuery = true;
        m_drive.executeOnBackgroundThread( [=, this, callback = request.m_callback]
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

        SIRIUS_ASSERT ( stream )

        stream->flush();

        SIRIUS_ASSERT ( stream->good())

        m_drive.executeOnSessionThread( [=, this]
                                        {
                                            onFlushed( true, callback );
                                        } );
    }

    void onFlushed( bool success, const std::function<void( std::optional<FlushResponse> )>& callback )
    {
        DBG_MAIN_THREAD

        SIRIUS_ASSERT ( m_isExecutingQuery )

        m_isExecutingQuery = false;

        if ( m_taskIsInterrupted )
        {
            callback( {} );
            finishManualTask();
            return;
        }

        callback( FlushResponse{success} );
    }

public:

    bool closeFile( const CloseFileRequest& request ) override
    {
        DBG_MAIN_THREAD

        SIRIUS_ASSERT ( m_upperSandboxFsTree )
        SIRIUS_ASSERT ( !m_isExecutingQuery )

        if ( m_taskIsInterrupted )
        {
            return false;
        }

        auto readIt = m_openFilesRead.find( request.m_fileId );

        if ( readIt != m_openFilesRead.end())
        {
            auto stream = readIt->second.m_stream;
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
            auto stream = writeIt->second.m_stream;
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

    bool removeFsTreeEntry( const RemoveFilesystemEntryRequest& request ) override
    {
        DBG_MAIN_THREAD

        SIRIUS_ASSERT ( m_upperSandboxFsTree )
        SIRIUS_ASSERT ( !m_isExecutingQuery )

        if ( m_taskIsInterrupted )
        {
            return false;
        }

        processRemoveRequest( request );

        return true;
    }

private:

    void closeStream( const std::weak_ptr<std::fstream>& weakStream, uint64_t fileId,
                      const std::function<void( std::optional<CloseFileResponse> )>& callback )
    {

        DBG_BG_THREAD

        auto stream = weakStream.lock();

        SIRIUS_ASSERT ( stream )

        try
        {
            stream->close();
        }
        catch ( ... )
        {
            _LOG_WARN( "Failed to close stream" )
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

        SIRIUS_ASSERT ( m_isExecutingQuery )

        m_isExecutingQuery = false;

        if ( m_taskIsInterrupted )
        {
            callback( {} );
            finishManualTask();
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

        _LOG_ERR( "Close nonexisting stream" )
    }

private:

    void processRemoveRequest( const RemoveFilesystemEntryRequest& request )
    {

        DBG_MAIN_THREAD

        if ( !checkUnlock( request.m_path ))
        {
            request.m_callback( RemoveFilesystemEntryResponse{false} );
            return;
        }

        auto* child = m_upperSandboxFsTree->getEntryPtr( request.m_path );

        if ( !child ) {
            request.m_callback( RemoveFilesystemEntryResponse{false} );
            return;
        }

        auto filesToRemove = obtainUnusedFiles(*child);

        m_upperSandboxFsTree->removeFlat( request.m_path, []( const auto& )
        {} );

        m_isExecutingQuery = true;
        m_drive.executeOnBackgroundThread(
                [this, callback = request.m_callback, filesToRemove = std::move( filesToRemove )]
                {
                    removeUnusedFiles( filesToRemove, [=, this] (uint64_t removedFilesSize) {
                        onRemoved(removedFilesSize, callback);
                    } );
                } );
    }

    void removeUnusedFiles( const std::set<InfoHash>& filesToRemove,
                            const std::function<void( uint64_t )>& callback )
    {
        DBG_BG_THREAD

        uint64_t removedFilesSize = 0;

        for ( const auto& file: filesToRemove )
        {
            try
            {
                auto filePath = m_drive.m_driveFolder / toString( file );
                removedFilesSize += fs::file_size(filePath);
                fs::remove(filePath);
            }
            catch ( const std::filesystem::filesystem_error& er )
            {
                _LOG_ERR( "Filesystem error has occurred " << er.what() << " "
                                                           << er.path1() << " "
                                                           << er.path2())
            }
        }

        m_drive.executeOnSessionThread( [=]
                                        {
                                            callback(removedFilesSize);
                                        } );
    }

    void onRemoved( uint64_t removedFilesSize,
                    const std::function<void( RemoveFilesystemEntryResponse )>& callback )
    {

        DBG_MAIN_THREAD

        SIRIUS_ASSERT ( m_isExecutingQuery )

        m_isExecutingQuery = false;

        if ( m_taskIsInterrupted )
        {
            callback( {} );
            finishManualTask();
            return;
        }

        SIRIUS_ASSERT ( removedFilesSize <= m_upperSandboxFilesSize )
        m_upperSandboxFilesSize -= removedFilesSize;

        callback( RemoveFilesystemEntryResponse{true} );
    }

private:

    std::set<InfoHash> obtainUnusedFiles(const Folder::Child& child) {
        std::set<InfoHash> possiblyRemovedFiles;

        if ( isFile( child ))
        {
            possiblyRemovedFiles = {getFile( child ).hash()};
        } else
        {
            getFolder( child ).getUniqueFiles( possiblyRemovedFiles );
        }

        std::set<InfoHash> filesToRemove;

        for ( const auto& hash: possiblyRemovedFiles )
        {
            auto it = m_callManagedHashes.find( hash );
            if ( it != m_callManagedHashes.end())
            {
                filesToRemove.insert( hash );
                m_callManagedHashes.erase( it );
            }
        }

        return filesToRemove;
    }

public:

    bool pathExist( const PathExistRequest& request ) override
    {
        DBG_MAIN_THREAD

        SIRIUS_ASSERT ( m_upperSandboxFsTree )
        SIRIUS_ASSERT ( !m_isExecutingQuery )

        if ( m_taskIsInterrupted )
        {
            return false;
        }

        auto* entry = m_upperSandboxFsTree->getEntryPtr( request.m_path );

        bool exists = entry != nullptr;

        request.m_callback(PathExistResponse{exists});

        return true;
    }

public:

    bool pathIsFile( const PathIsFileRequest& request ) override {
        DBG_MAIN_THREAD

        SIRIUS_ASSERT ( m_upperSandboxFsTree )
        SIRIUS_ASSERT ( !m_isExecutingQuery )

        if ( m_taskIsInterrupted )
        {
            return false;
        }

        auto* entry = m_upperSandboxFsTree->getEntryPtr( request.m_path );

        if(entry == nullptr)
        {
            request.m_callback(PathIsFileResponse{false});
        }
        else
        {
            request.m_callback(PathIsFileResponse{isFile(*entry)});
        }

        return true;
    }

public:

    bool fileSize( const FileSizeRequest& request ) override
    {
        DBG_MAIN_THREAD

        SIRIUS_ASSERT ( m_upperSandboxFsTree )
        SIRIUS_ASSERT ( !m_isExecutingQuery )

        if ( m_taskIsInterrupted )
        {
            return false;
        }

        processFileSizeRequest(request);
        return true;
    }

    void processFileSizeRequest(const FileSizeRequest& request) {
        DBG_MAIN_THREAD

        auto* entry = m_upperSandboxFsTree->getEntryPtr( request.m_path );

        if (!entry) {
            request.m_callback(FileSizeResponse{});
            return;
        }

        if (!isFile(*entry)) {
            request.m_callback(FileSizeResponse{});
            return;
        }

        // Note: field "size" of file is not updated for files created during the manual modification
        const auto& file = getFile( *entry );
        auto name = toString( file.hash());

        auto absolutePath = m_drive.m_driveFolder / name;

        m_isExecutingQuery = true;
        m_drive.executeOnBackgroundThread(
                [this, callback = request.m_callback, path = absolutePath]
                {
                    obtainFileSize( path.string(), callback );
                } );
    }

private:

    void obtainFileSize(const std::string& path,
                        const std::function<void( std::optional<FileSizeResponse> )>& callback ) {
        DBG_BG_THREAD

        std::error_code ec;
        auto size = fs::file_size(path, ec);

        if ( ec )
        {
            _LOG_ERR( "Error during obtaining file size: " << ec.message())
        }

        m_drive.executeOnSessionThread( [=, this]
        {
            onFileSizeObtained( size, callback );
        } );
    }

    void onFileSizeObtained(uint64_t size,
                            const std::function<void( std::optional<FileSizeResponse> )>& callback) {
        DBG_MAIN_THREAD

        SIRIUS_ASSERT ( m_isExecutingQuery )

        m_isExecutingQuery = false;

        if ( m_taskIsInterrupted )
        {
            callback( {} );
            finishManualTask();
            return;
        }

        callback(FileSizeResponse{true, size});
    }

public:

    bool createDirectories( const CreateDirectoriesRequest& request ) override
    {
        DBG_MAIN_THREAD

        SIRIUS_ASSERT ( m_upperSandboxFsTree )
        SIRIUS_ASSERT ( !m_isExecutingQuery )

        if ( m_taskIsInterrupted )
        {
            return false;
        }

        bool success = m_upperSandboxFsTree->addFolder( request.m_path );

        request.m_callback( CreateDirectoriesResponse{success} );

        return true;
    }

public:

    bool folderIteratorCreate( const FolderIteratorCreateRequest& request ) override
    {
        DBG_MAIN_THREAD

        SIRIUS_ASSERT ( m_upperSandboxFsTree )
        SIRIUS_ASSERT ( !m_isExecutingQuery )

        if ( m_taskIsInterrupted )
        {
            return false;
        }

        if ( checkUnlock( request.m_path ))
        {
            auto* pFolder = m_upperSandboxFsTree->getFolderPtr( request.m_path );
            if ( pFolder != nullptr )
            {
                auto iteratorId = m_totalFolderIteratorsCreated;
                m_totalFolderIteratorsCreated++;
                m_folderIterators.try_emplace( iteratorId, *pFolder, request.m_recursive );
                request.m_callback( FolderIteratorCreateResponse{iteratorId} );
            } else
            {
                request.m_callback( FolderIteratorCreateResponse{} );
            }
        } else
        {
            request.m_callback( FolderIteratorCreateResponse{} );
        }

        return true;
    }

    bool folderIteratorDestroy( const FolderIteratorDestroyRequest& request ) override
    {
        DBG_MAIN_THREAD

        SIRIUS_ASSERT ( m_upperSandboxFsTree )
        SIRIUS_ASSERT ( !m_isExecutingQuery )

        if ( m_taskIsInterrupted )
        {
            return false;
        }

        auto it = m_folderIterators.find( request.m_id );

        if ( it != m_folderIterators.end())
        {
            m_folderIterators.erase( it );
            request.m_callback( FolderIteratorDestroyResponse{true} );
        } else
        {
            request.m_callback( FolderIteratorDestroyResponse{false} );
        }

        return true;
    }

    bool folderIteratorHasNext( const FolderIteratorHasNextRequest& request ) override
    {
        DBG_MAIN_THREAD

        SIRIUS_ASSERT ( m_upperSandboxFsTree )
        SIRIUS_ASSERT ( !m_isExecutingQuery )

        if ( m_taskIsInterrupted )
        {
            return false;
        }

        auto it = m_folderIterators.find( request.m_id );

        if ( it != m_folderIterators.end())
        {
            request.m_callback( FolderIteratorHasNextResponse{it->second.hasNext()} );
        } else
        {
            request.m_callback( FolderIteratorHasNextResponse{false} );
        }
        return true;
    }

    bool folderIteratorNext( const FolderIteratorNextRequest& request ) override
    {
        DBG_MAIN_THREAD

        SIRIUS_ASSERT ( m_upperSandboxFsTree )
        SIRIUS_ASSERT ( !m_isExecutingQuery )

        if ( m_taskIsInterrupted )
        {
            return false;
        }

        auto it = m_folderIterators.find( request.m_id );

        if ( it == m_folderIterators.end())
        {
            request.m_callback( FolderIteratorNextResponse{} );
            return true;
        }

        auto iteratorValue = it->second.next();

        if (!iteratorValue) {
            request.m_callback( FolderIteratorNextResponse{} );
            return true;
        }

        request.m_callback( FolderIteratorNextResponse{true,
                                                       iteratorValue->m_name,
                                                       iteratorValue->m_depth} );

        return true;
    }

public:

    bool moveFsTreeEntry( const MoveFilesystemEntryRequest& request ) override
    {
        DBG_MAIN_THREAD

        SIRIUS_ASSERT ( m_upperSandboxFsTree )
        SIRIUS_ASSERT ( !m_isExecutingQuery )

        if ( m_taskIsInterrupted )
        {
            return false;
        }

        if ( !checkUnlock( request.m_src ) || !checkUnlock( request.m_dst ) ) {
            request.m_callback( MoveFilesystemEntryResponse{false} );
            return true;
        }

        auto* dstChild = m_upperSandboxFsTree->getEntryPtr( request.m_dst );

        std::set<InfoHash> filesToRemove;
        if (dstChild) {
            filesToRemove = obtainUnusedFiles(*dstChild);
        }

        if ( !m_upperSandboxFsTree->moveFlat( request.m_src, request.m_dst, []( const auto& )
        {} ))
        {
            request.m_callback( MoveFilesystemEntryResponse{false} );
            return true;
        }

        m_isExecutingQuery = true;
        m_drive.executeOnBackgroundThread(
                [this, callback = request.m_callback, filesToRemove = std::move( filesToRemove )]
                {
                    removeUnusedFiles( filesToRemove, [=, this] (uint64_t removedFilesSize) {
                        onMoved(removedFilesSize, callback);
                    } );
                } );

        return true;
    }

private:

    void onMoved( uint64_t removedFilesSize,
                  const std::function<void( MoveFilesystemEntryResponse )>& callback )
    {

        DBG_MAIN_THREAD

        SIRIUS_ASSERT ( m_isExecutingQuery )

        m_isExecutingQuery = false;

        if ( m_taskIsInterrupted )
        {
            callback( {} );
            finishManualTask();
            return;
        }

        SIRIUS_ASSERT ( removedFilesSize <= m_upperSandboxFilesSize )
        m_upperSandboxFilesSize -= removedFilesSize;

        callback( MoveFilesystemEntryResponse{true} );
    }

public:

    bool applySandboxModifications( const ApplySandboxModificationsRequest& request ) override
    {
        DBG_MAIN_THREAD

        SIRIUS_ASSERT ( !m_isExecutingQuery )

        if ( m_taskIsInterrupted )
        {
            return false;
        }

        SIRIUS_ASSERT ( m_upperSandboxFsTree )

        closeFiles( [=, this]
                    {
                        onSandboxModificationStreamsClosed( request );
                    } );

        return true;
    }

private:

    void onSandboxModificationStreamsClosed( const ApplySandboxModificationsRequest& request )
    {
        DBG_MAIN_THREAD

        SIRIUS_ASSERT ( m_isExecutingQuery )

        m_isExecutingQuery = false;

        if ( m_taskIsInterrupted )
        {
            request.m_callback( {} );
            finishManualTask();
            return;
        }

        m_folderIterators.clear();

        m_upperSandboxFsTree->clearStatisticsNode();

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
    }

    void discardUpperSandboxModifications(
            const std::function<void( std::optional<ApplySandboxModificationsResponse> )>& callback )
    {
        DBG_BG_THREAD

        uint64_t removedFilesSize = 0;

        for ( const auto& hash: m_callManagedHashes )
        {
            SIRIUS_ASSERT ( hash != Hash256() )
            std::error_code ec;
            auto filePath = m_drive.m_driveFolder / toString( hash );
            removedFilesSize += fs::file_size( filePath, ec );
            fs::remove( filePath, ec );

            if ( ec )
            {
                _LOG_ERR( "Error during removing file: " << ec.message())
            }
        }

        m_drive.executeOnSessionThread( [=, this]
                                        {
                                            onAppliedSandboxModifications( false, removedFilesSize, callback );
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

        uint64_t removedFilesSize = 0;

        for ( const auto& file: lowerUniqueFiles )
        {
            if ( !upperUniqueFiles.contains( file ) &&
                 !m_drive.m_torrentHandleMap.contains( file ) &&
                 file != Hash256())
            {
                std::error_code ec;
                auto filePath = m_drive.m_driveFolder / toString( file );
                removedFilesSize += fs::file_size( filePath, ec );
                fs::remove( filePath, ec );

                if ( ec )
                {
                    _LOG_ERR( "Error during removing file: " << ec.message())
                }
            }
        }

        m_drive.executeOnSessionThread( [=, this]
                                        {
                                            onAppliedSandboxModifications( true, removedFilesSize, callback );
                                        } );
    }

    void onAppliedSandboxModifications( bool success,
                                        uint64_t removedFilesSize,
                                        const std::function<void(
                                                std::optional<ApplySandboxModificationsResponse> )>& callback )
    {

        DBG_MAIN_THREAD

        SIRIUS_ASSERT ( m_isExecutingQuery )

        m_isExecutingQuery = false;

        if ( m_taskIsInterrupted )
        {
            callback( {} );
            finishManualTask();
            return;
        }

        SIRIUS_ASSERT (removedFilesSize <= m_upperSandboxFilesSize)
        m_upperSandboxFilesSize -= removedFilesSize;

        if ( success )
        {
            m_lowerSandboxFsTree = std::move( m_upperSandboxFsTree );
            m_lowerSandboxFilesSize = m_upperSandboxFilesSize;
        } else
        {
            m_upperSandboxFsTree.reset();
            SIRIUS_ASSERT (m_lowerSandboxFilesSize == m_upperSandboxFilesSize)
            m_upperSandboxFilesSize = 0;
        }
        m_callManagedHashes.clear();

        callback( ApplySandboxModificationsResponse{success, 0, 0} );
    }

public:

    bool evaluateStorageHash( const EvaluateStorageHashRequest& request ) override
    {
        DBG_MAIN_THREAD

        SIRIUS_ASSERT ( !m_isExecutingQuery )

        if ( m_taskIsInterrupted )
        {
            return false;
        }

        SIRIUS_ASSERT ( !m_upperSandboxFsTree )

        m_isExecutingQuery = true;
        m_drive.executeOnBackgroundThread( [this, callback = request.m_callback]
                                           {
                                               computeSandboxHash( callback );
                                           } );

        return true;
    }

private:

    void computeSandboxHash( const std::function<void( std::optional<EvaluateStorageHashResponse> )> callback )
    {
        DBG_BG_THREAD

        m_lowerSandboxFsTree->mapFiles( [this]( File& file )
                                        {
                                            if ( file.isModifiable())
                                            {
                                                auto filePath = m_drive.m_driveFolder / toString( file.hash());
                                                auto torrentPath = m_drive.m_torrentFolder / toString( file.hash());

                                                auto size = fs::file_size( filePath );

                                                auto hash = size > 0 ?
                                                		calculateInfoHash(filePath, m_drive.m_driveKey) : Hash256();

                                                try
                                                {
                                                	if ( hash == Hash256() || m_drive.m_torrentHandleMap.contains( hash ) )
                                                	{
                                                		fs::remove( filePath );
                                                	} else
                                                	{
                                                        moveFile( filePath, m_drive.m_driveFolder / toString( hash ));
                                                		createTorrentFile( m_drive.m_driveFolder / toString( hash ),
																		   m_drive.m_driveKey,
																		   m_drive.m_driveFolder,
                                                                           m_drive.m_torrentFolder / toString( hash ) );
                                                	}
                                                    file.setHash( hash );
                                                    file.setSize( size );
                                                    file.setIsModifiable( false );
                                                }
                                                catch ( const std::filesystem::filesystem_error& er )
                                                {
                                                    _LOG_ERR( "Filesystem error has occurred " << er.what() << " "
                                                                                               << er.path1() << " "
                                                                                               << er.path2())
                                                }
                                            }
                                        } );

        m_lowerSandboxFsTree->doSerialize( m_drive.m_sandboxFsTreeFile.string());

        m_sandboxRootHash = createTorrentFile( m_drive.m_sandboxFsTreeFile,
                                               m_drive.m_driveKey,
                                               m_drive.m_sandboxRootPath,
                                               m_drive.m_sandboxFsTreeTorrent);

        m_opinionTaskController.notApprovedModificationId() = m_request->m_modificationIdentifier.array();
        m_opinionTaskController.saveNotApprovedCumulativeUploadsForManualTask();

        m_drive.executeOnSessionThread( [=, this]
                                        {
                                            _LOG( "todo: Execution on bg thread: onStorageHashEvaluated()" );
                                            onStorageHashEvaluated( *m_sandboxRootHash, callback );
                                        } );
    }

    void onStorageHashEvaluated( const InfoHash& storageHash,
                                 const std::function<void( std::optional<EvaluateStorageHashResponse> )> callback )
    {
        _LOG( "todo: onStorageHashEvaluated()" );
        DBG_MAIN_THREAD

        SIRIUS_ASSERT ( m_isExecutingQuery )

        m_isExecutingQuery = false;

        if ( m_taskIsInterrupted )
        {
            callback( {} );
            finishManualTask();
            return;
        }

        callback( EvaluateStorageHashResponse{storageHash, 0, 0, 0} );
    }

public:

    bool applyStorageModifications( const ApplyStorageModificationsRequest& request ) override
    {
        DBG_MAIN_THREAD

        SIRIUS_ASSERT ( !m_isExecutingQuery )

        if ( m_taskIsInterrupted )
        {
            return false;
        }

        SIRIUS_ASSERT ( !m_upperSandboxFsTree )

        if ( request.m_success )
        {
            m_drive.executeOnBackgroundThread( [this, callback = request.m_callback]
                                               {
                                                   addNewTorrentsToSession( callback );
                                               } );
        } else
        {
            request.m_callback( ApplyStorageModificationsResponse{} );
            finishManualTask();
        }

        return true;
    }

private:

    void
    addNewTorrentsToSession(
            const std::function<void( std::optional<ApplyStorageModificationsResponse> )>& callback )
    {
        DBG_BG_THREAD

        std::set<InfoHash> uniqueFiles;
        m_lowerSandboxFsTree->getUniqueFiles( uniqueFiles );
        for ( const auto& file: uniqueFiles )
        {
            if ( auto it = m_drive.m_torrentHandleMap.find( file ); file != Hash256() &&
                                                                    it == m_drive.m_torrentHandleMap.end())
            {
                auto session = m_drive.m_session.lock();
                if ( session )
                {
                    std::string fileName = toString( file );

                    m_drive.m_torrentHandleMap[file].m_ltHandle = session->addTorrentFileToSession(
                            m_drive.m_torrentFolder / fileName,
                            m_drive.m_driveFolder,
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
            SIRIUS_ASSERT ( it.first != Hash256() )
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
    onUnusedTorrentsDeleted(
            const std::function<void( std::optional<ApplyStorageModificationsResponse> )>& callback )
    {
        DBG_BG_THREAD

        try
        {
            moveFile( m_drive.m_sandboxFsTreeFile, m_drive.m_fsTreeFile );
            moveFile( m_drive.m_sandboxFsTreeTorrent, m_drive.m_fsTreeTorrent );

            m_opinionTaskController.saveRestartValuesForManualTask( m_request->m_modificationIdentifier );
            //m_drive.m_serializer.saveRestartValue( m_request->m_modificationIdentifier, "approvedModification" );

            auto& torrentHandleMap = m_drive.m_torrentHandleMap;

            // remove unused files and torrent files from the drive
            for ( const auto& it : torrentHandleMap )
            {
                const UseTorrentInfo& info = it.second;
                if ( !info.m_isUsed )
                {
                    const auto& hash = it.first;
                    SIRIUS_ASSERT ( hash != Hash256() )
                    std::string filename = hashToFileName( hash );
                    fs::remove( fs::path( m_drive.m_driveFolder ) / filename );
                    fs::remove( fs::path( m_drive.m_torrentFolder ) / filename );
                }
            }

            // Add FsTree torrent to session
            if ( auto session = m_drive.m_session.lock(); session )
            {
                m_sandboxFsTreeHandle = session->addTorrentFileToSession( m_drive.m_fsTreeTorrent,
                                                                          m_drive.m_fsTreeTorrent.parent_path(),
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
            _LOG_ERR( "Exception during unused file delition: " << ex.what())
        }
    }

    void onAppliedStorageModifications(
            const std::function<void( std::optional<ApplyStorageModificationsResponse> )>& callback )
    {
        DBG_MAIN_THREAD

        SIRIUS_ASSERT ( m_sandboxRootHash )
        SIRIUS_ASSERT ( m_sandboxFsTreeHandle )

        m_drive.m_fsTree = std::move( m_lowerSandboxFsTree );
        m_drive.m_rootHash = *m_sandboxRootHash;
        m_drive.m_fsTreeLtHandle = *m_sandboxFsTreeHandle;
        m_drive.m_lastApprovedModification = m_request->m_modificationIdentifier;

        callback( ApplyStorageModificationsResponse{} );

        finishManualTask();
    }

public:

    void shutdown() override
    {
        terminateManualTask();
    }
    
    void terminateManualTask()
    {
        DBG_MAIN_THREAD

        if ( m_taskIsInterrupted )
        {
            return;
        }

        m_taskIsInterrupted = true;

        if ( !m_isExecutingQuery )
        {
            finishManualTask();
        }
    }

    bool onApprovalTxPublished( const PublishedModificationApprovalTransactionInfo& transaction ) override
    {
        DBG_MAIN_THREAD

        terminateManualTask();

        return true;
    }

    void onDriveClose( const DriveClosureRequest& closureRequest ) override
    {
        DBG_MAIN_THREAD

        terminateManualTask();
    }

    void onModificationInitiated( const ModificationRequest& request ) override
    {
        DBG_MAIN_THREAD

        // It is possible if the contract has been destroyed because of unsuccessful deployment

        terminateManualTask();
    }

    void onStreamStarted( const StreamRequest& request ) override
    {
        DBG_MAIN_THREAD

        // It is possible if the contract has been destroyed because of unsuccessful deployment

        terminateManualTask();
    }

    void onManualModificationInitiated( const InitiateModificationsRequest& request ) override
    {
        DBG_MAIN_THREAD

//        SIRIUS_ASSERT ( m_request->m_modificationIdentifier == request.m_modificationIdentifier )

        terminateManualTask();
    }

    bool manualSynchronize( const SynchronizationRequest& request ) override
    {
        DBG_MAIN_THREAD

        terminateManualTask();

        return true;
    }

private:

    bool checkUnlock( const std::string& path )
    {
        bool noLocks = true;
        m_upperSandboxFsTree->iterateBranch( path,
                                             [&]( const Folder& folder ) -> bool
                                             {
                                                 if ( folder.statisticsNode()->statistics().m_blocks >
                                                      0 )
                                                 {
                                                     noLocks = false;
                                                     return false;
                                                 }
                                                 return true;
                                             }, [&]( const Folder::Child& child ) -> bool
                                             {
                                                 uint64_t numLocks = 0;
                                                 if ( isFolder( child ))
                                                 {
                                                     numLocks = getFolder(
                                                             child ).statisticsNode()->statistics().totalBlocks();
                                                 } else
                                                 {
                                                     numLocks = getFile(
                                                             child ).statisticsNode()->statistics().totalBlocks();
                                                 }

                                                 if ( numLocks > 0 )
                                                 {
                                                     noLocks = false;
                                                     return false;
                                                 }

                                                 return true;
                                             } );

        return noLocks;
    }

protected:
    void finishManualTask()
    {
        DBG_MAIN_THREAD

        if (m_taskIsFinished) {
            return;
        }

        m_taskIsFinished = true;

        m_drive.executeOnBackgroundThread([this] {
            clearTrash();
        });
    }

private:

    void clearTrash()
    {
        DBG_BG_THREAD

        std::set<InfoHash> sandboxFiles;
        if (m_upperSandboxFsTree)
        {
            m_upperSandboxFsTree->getUniqueFiles(sandboxFiles);
        }
        if (m_lowerSandboxFsTree)
        {
            m_lowerSandboxFsTree->getUniqueFiles(sandboxFiles);
        }

        for (const auto& file: sandboxFiles) {
            if (!m_drive.m_torrentHandleMap.contains(file))
            {
                std::error_code ec;
                fs::remove(m_drive.m_driveFolder / toString(file), ec);
                fs::remove(m_drive.m_torrentFolder / toString(file), ec);
            }
        }

        m_drive.executeOnSessionThread([this] {
            closeFiles( [this]
            {
                finishTaskAndRunNext();
            } );
        });
    }

    void closeFiles( const std::function<void()>& callback )
    {
        DBG_MAIN_THREAD

        std::vector<std::shared_ptr<std::fstream>> streams;

        for ( auto& file: m_openFilesWrite )
        {
            streams.emplace_back( std::move( file.second.m_stream ));
        }
        m_openFilesWrite.clear();

        for ( auto& file: m_openFilesRead )
        {
            streams.emplace_back( std::move( file.second.m_stream ));
        }
        m_openFilesRead.clear();

        m_isExecutingQuery = true;
        m_drive.executeOnBackgroundThread( [=, this]
                                           {
                                               closeStreams( streams, callback );
                                           } );
    }

    void closeStreams( const std::vector<std::shared_ptr<std::fstream>>& streams,
                       const std::function<void()>& callback )
    {
        DBG_BG_THREAD

        for ( const auto& stream: streams )
        {
            stream->close();
        }

        m_drive.executeOnSessionThread( [=]
                                        {
                                            callback();
                                        } );
    }
};

std::unique_ptr<DriveTaskBase> createManualModificationsTask( mobj<InitiateModificationsRequest>&& request,
                                                              DriveParams& drive,
                                                               ModifyOpinionController& opinionTaskController)
{
    return std::make_unique<ManualModificationsTask>( std::move( request ), drive, opinionTaskController );
}

}

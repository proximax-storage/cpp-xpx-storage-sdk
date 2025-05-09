/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include <vector>
#include <optional>
#include "types.h"
#include "FsTree.h"

namespace sirius::drive
{

struct InitiateModificationsResponse
{

};

struct InitiateModificationsRequest
{
    Hash256 m_modificationIdentifier;
    std::function<void( std::optional<InitiateModificationsResponse> )> m_callback;
};

struct InitiateSandboxModificationsResponse
{

};

struct InitiateSandboxModificationsRequest
{
    std::vector<std::string> m_serviceFolders;
    std::function<void( std::optional<InitiateSandboxModificationsResponse> )> m_callback;

    InitiateSandboxModificationsRequest(
            std::function<void( std::optional<InitiateSandboxModificationsResponse> )>&& callback )
            : m_callback( std::move( callback )) {}

    InitiateSandboxModificationsRequest(
            const std::vector<std::string>& serviceFolders,
            std::function<void( std::optional<InitiateSandboxModificationsResponse> )>&& callback )
            : m_serviceFolders( serviceFolders )
            , m_callback( std::move( callback )) {}
};

enum class OpenFileMode
{
    READ, WRITE
};

struct OpenFileResponse
{
    std::optional<uint64_t> m_fileId;
};

struct OpenFileRequest
{
    OpenFileMode m_mode;
    std::string m_path;
    std::function<void( std::optional<OpenFileResponse> )> m_callback;
};

struct ReadFileResponse
{
    std::optional<std::vector<uint8_t>> m_buffer;
};

struct ReadFileRequest
{
    uint64_t m_fileId;
    uint64_t m_bytes;
    std::function<void( std::optional<ReadFileResponse> )> m_callback;
};

struct WriteFileResponse
{
    bool m_success;
};

struct WriteFileRequest
{
    uint64_t m_fileId;
    std::vector<uint8_t> m_buffer;
    std::function<void( std::optional<WriteFileResponse> )> m_callback;
};

struct FlushResponse
{
    bool m_success;
};

struct FlushRequest
{
    uint64_t m_fileId;
    std::function<void( std::optional<FlushResponse> )> m_callback;
};

struct CloseFileResponse
{
    bool m_success;
};

struct CloseFileRequest
{
    uint64_t m_fileId;
    std::function<void( std::optional<CloseFileResponse> )> m_callback;
};

struct RemoveFilesystemEntryResponse
{
    bool m_success;
};

struct RemoveFilesystemEntryRequest
{
    std::string m_path;
    std::function<void( std::optional<RemoveFilesystemEntryResponse> )> m_callback;
};

struct CreateDirectoriesResponse
{
    bool m_success;
};

struct CreateDirectoriesRequest
{
    std::string m_path;
    std::function<void( std::optional<CreateDirectoriesResponse> )> m_callback;
};

struct MoveFilesystemEntryResponse
{
    bool m_success;
};

struct MoveFilesystemEntryRequest
{
    std::string m_src;
    std::string m_dst;
    std::function<void( std::optional<MoveFilesystemEntryResponse> )> m_callback;
};

struct ApplySandboxModificationsResponse
{
    bool m_success;
    int64_t m_sandboxSizeDelta;
    int64_t m_stateSizeDelta;
};

struct ApplySandboxModificationsRequest
{
    bool m_success;
    std::function<void( std::optional<ApplySandboxModificationsResponse> )> m_callback;
};

struct EvaluateStorageHashResponse
{
    InfoHash m_state;
    uint64_t m_usedDriveSize;
    uint64_t m_metaFilesSize;
    uint64_t m_fileStructureSize;
};

struct EvaluateStorageHashRequest
{
    std::function<void( std::optional<EvaluateStorageHashResponse> )> m_callback;
};

struct ApplyStorageModificationsResponse
{
};

struct ApplyStorageModificationsRequest
{
    bool m_success;
    std::function<void( std::optional<ApplyStorageModificationsResponse> )> m_callback;
};

struct SynchronizationResponse
{
    bool success;
};

struct SynchronizationRequest
{
    Hash256 m_modificationIdentifier;
    Hash256 m_rootHash;
    std::function<void( std::optional<SynchronizationResponse> )> m_callback;
};

struct FileInfoResponse
{
    bool m_exists = false;
    std::string m_path;
    uint64_t m_size;
};

struct FileInfoRequest
{
    std::string m_relativePath;
    std::function<void( std::optional<FileInfoResponse> )> m_callback;
};

struct ActualModificationIdResponse
{
    Hash256 m_modificationId;
};

struct ActualModificationIdRequest
{
    std::function<void( std::optional<ActualModificationIdResponse> )> m_callback;
};

struct FilesystemResponse
{
    FsTree m_fsTree;
};

struct FilesystemRequest
{
    std::function<void( std::optional<FilesystemResponse> )> m_callback;
};

struct FolderIteratorCreateResponse
{
    std::optional<uint64_t> m_id;
};

struct FolderIteratorCreateRequest
{
    std::string m_path;
    bool m_recursive;
    std::function<void( std::optional<FolderIteratorCreateResponse> )> m_callback;
};

struct FolderIteratorHasNextResponse
{
    bool m_hasNext;
};

struct FolderIteratorHasNextRequest
{
    uint64_t m_id;
    std::function<void( std::optional<FolderIteratorHasNextResponse> )> m_callback;
};

struct FolderIteratorNextResponse
{
    bool m_valid = false;
    std::string m_name;
    uint32_t m_depth;
};

struct FolderIteratorNextRequest
{
    uint64_t m_id;
    std::function<void( std::optional<FolderIteratorNextResponse> )> m_callback;
};

struct FolderIteratorDestroyResponse
{
    bool success;
};

struct FolderIteratorDestroyRequest
{
    uint64_t m_id;
    std::function<void( std::optional<FolderIteratorDestroyResponse> )> m_callback;
};

struct PathExistResponse
{
    bool m_exists;
};

struct PathExistRequest
{
    std::string m_path;
    std::function<void( std::optional<PathExistResponse> )> m_callback;
};

struct PathIsFileResponse
{
    bool m_isFile;
};

struct PathIsFileRequest
{
    std::string m_path;
    std::function<void( std::optional<PathIsFileResponse> )> m_callback;
};

struct FileSizeResponse
{
    bool m_success = false;
    uint64_t m_size = 0;
};

struct FileSizeRequest
{
    std::string m_path;
    std::function<void( std::optional<FileSizeResponse> )> m_callback;
};

}
/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include <vector>


namespace sirius::drive
{

struct InitiateModificationsResponse
{

};

struct InitiateModificationsRequest
{
    std::function<void( InitiateModificationsResponse )> m_callback;
};

struct InitiateSandboxModificationsResponse
{

};

struct InitiateSandboxModificationsRequest
{
    std::function<void( InitiateSandboxModificationsResponse )> m_callback;
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
    std::function<void( OpenFileResponse )> m_callback;
};

struct ReadFileResponse
{
    std::optional<std::vector<uint8_t>> m_buffer;
};

struct ReadFileRequest
{
    uint64_t m_fileId;
    uint64_t m_bytes;
    std::function<void( ReadFileResponse )> m_callback;
};

struct WriteFileResponse
{
    bool m_success;
};

struct WriteFileRequest
{
    uint64_t m_fileId;
    std::vector<uint8_t> m_buffer;
    std::function<void( WriteFileResponse )> m_callback;
};

struct FlushResponse
{
    bool m_success;
};

struct FlushRequest
{
    uint64_t m_fileId;
    std::function<void( FlushResponse )> m_callback;
};

struct CloseFileResponse
{
    bool m_success;
};

struct CloseFileRequest
{
    uint64_t m_fileId;
    std::function<void( CloseFileResponse )> m_callback;
};

struct ApplySandboxModificationsResponse
{
    bool m_success;
    int64_t sandbox_size_delta;
    int64_t state_size_delta;
};

struct ApplySandboxModificationsRequest
{
    bool m_success;
    std::function<void (ApplySandboxModificationsResponse)> m_callback;
};

struct EvaluateStorageHashResponse {
    uint64_t m_usedDriveSize;
    uint64_t m_metaFilesSize;
    uint64_t m_fileStructureSize;
};

struct EvaluateStorageHashRequest
{
    std::function<void (EvaluateStorageHashResponse)> m_callback;
};

struct ApplyStorageModificationsResponse {
    bool m_success;
};

struct ApplyStorageModificationsRequest
{
    bool m_success;
    std::function<void (ApplyStorageModificationsResponse)> m_callback;
};

}
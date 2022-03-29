/*
*** Copyright 2022 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "types.h"

namespace sirius::drive {

    // если размер sandbox-a позволяет, то мы храним все чанки
    // иначе удаляем старые
    //
    struct StreamRequest
    {
        Hash256                     m_streamId; // transaction hash
        std::string                 m_folder; // where it will be saved
        uint64_t                    m_maxSizeBytes; // could be increased
    };

    struct StreamIncreaseRequest
    {
        Hash256                     m_streamId; // transaction hash
        uint64_t                    m_maxSizeBytes; // stream size after increasing
    };

    struct StreamFinishRequest
    {
        Hash256                     m_streamId; // transaction hash
        InfoHash                    m_streamStructureInfoHash;
        uint64_t                    m_streamSizeBytes; // stream size after increasing
    };

    struct StreamPlayListDescription
    {
        Hash256                     m_streamId;
        Key                         m_driveKey;
        InfoHash                    m_playlistInfoHash;
        int                         m_chunkIndex; // number of 1-st chunk
        Signature                   m_sign;
    };

    struct StreamChunk
    {
        int                         m_durationMs;
        uint64_t                    m_size;
        InfoHash                    m_hash;
    };

    struct StreamPlayList
    {
        Hash256                     m_streamId;
        int                         m_version;
        int                         m_chunkIndex; // number of 1-st chunk
        std::vector<StreamChunk>    m_chunks;
    };

    struct ClientRequest
    {
        Hash256                     m_streamId;
        Key                         m_driveKey;
        //int   m_timeOffsetMs;
    };
}

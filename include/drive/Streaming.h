/*
*** Copyright 2022 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "types.h"
#include "crypto/Signer.h"

#include <cereal/types/vector.hpp>
#include <cereal/types/array.hpp>
#include <cereal/types/map.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/archives/portable_binary.hpp>


namespace sirius::drive {

    // если размер sandbox-a позволяет, то мы храним все чанки
    // иначе удаляем старые
    //
    struct StreamRequest
    {
        Hash256                     m_streamId;     // transaction hash
        Key                         m_streamerKey;  // streamer public key
        std::string                 m_folder;       // where it will be saved
        uint64_t                    m_maxSizeBytes; // could be increased
        ReplicatorList              m_replicatorList;
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

    struct ChunkInfo
    {
        std::array<uint8_t,32>      m_streamId;
        uint32_t                    m_chunkIndex;
        std::array<uint8_t,32>      m_chunkInfoHash;
        uint32_t                    m_durationMks; // microseconds
        uint64_t                    m_sizeBytes;
        std::array<uint8_t, 64>     m_sign;

        template <class Archive> void serialize( Archive & arch )
        {
            arch( m_streamId );
            arch( m_chunkIndex );
            arch( m_chunkInfoHash );
            arch( m_durationMks );
            arch( m_sizeBytes );
            arch( m_sign );
        }

        void Sign( const crypto::KeyPair& keyPair )
        {
            crypto::Sign( keyPair,
                          {
                                utils::RawBuffer{m_streamId},
                                utils::RawBuffer{ (const uint8_t*) &m_chunkIndex, sizeof(m_chunkIndex) },
                                utils::RawBuffer{m_chunkInfoHash},
                                utils::RawBuffer{ (const uint8_t*) &m_durationMks, sizeof(m_durationMks) },
                                utils::RawBuffer{ (const uint8_t*) &m_sizeBytes,  sizeof(m_sizeBytes)  },
                          },
                         reinterpret_cast<Signature &>(m_sign) );
        }

        bool Verify( const Key& streamerKey ) const
        {
            return crypto::Verify( streamerKey,
                                   {
                                        utils::RawBuffer{m_streamId},
                                        utils::RawBuffer{ (const uint8_t*) &m_chunkIndex, sizeof(m_chunkIndex) },
                                        utils::RawBuffer{m_chunkInfoHash},
                                        utils::RawBuffer{ (const uint8_t*) &m_durationMks, sizeof(m_durationMks) },
                                        utils::RawBuffer{ (const uint8_t*) &m_sizeBytes,  sizeof(m_sizeBytes)  },
                                   },
                                  reinterpret_cast<const Signature &>(m_sign) );
        }
    };

    struct ChunkRequest
    {
        Hash256                     m_streamId;
        uint32_t                    m_chunkIndex;
    };
    
//    struct StreamPlayListInfo
//    {
//        Hash256                     m_streamId;
//        Key                         m_driveKey;
//        InfoHash                    m_playlistInfoHash;
//        uint32_t                    m_chunkIndex; // number of 1-st chunk
//        Signature                   m_sign;
//    };
//
//    struct StreamChunk
//    {
//        int                         m_durationMs;
//        uint64_t                    m_size;
//        InfoHash                    m_hash;
//    };
//
//    struct StreamPlayList
//    {
//        Hash256                     m_streamId;
//        int                         m_version;
//        int                         m_chunkIndex; // number of 1-st chunk
//        std::vector<StreamChunk>    m_chunks;
//    };
//
//    struct ClientRequest
//    {
//        Hash256                     m_streamId;
//        Key                         m_driveKey;
//        //int   m_timeOffsetMs;
//    };

    struct FinishStreamChunkInfo
    {
        uint32_t                    m_chunkIndex;
        std::array<uint8_t,32>      m_chunkInfoHash;
        uint32_t                    m_durationMks;   // microseconds
        uint64_t                    m_sizeBytes;
        bool                        m_saveOnDrive;

        template <class Archive> void serialize( Archive & arch )
        {
            arch( m_chunkIndex );
            arch( m_chunkInfoHash );
            arch( m_durationMks );
            arch( m_sizeBytes );
            arch( m_saveOnDrive );
        }
    };

    struct FinishStream
    {
        std::array<uint8_t,32>      m_streamId;
        std::array<uint8_t,32>      m_finishDataInfoHash;
        std::array<uint8_t, 64>     m_sign;

        template <class Archive> void serialize( Archive & arch )
        {
            arch( m_streamId );
            arch( m_finishDataInfoHash );
            arch( m_sign );
        }

        void Sign( const crypto::KeyPair& keyPair )
        {
            crypto::Sign( keyPair,
                          {
                                utils::RawBuffer{m_streamId},
                                utils::RawBuffer{m_finishDataInfoHash},
                          },
                         reinterpret_cast<Signature &>(m_sign) );
        }

        bool Verify( const Key& streamerKey ) const
        {
            return crypto::Verify( streamerKey,
                                   {
                                        utils::RawBuffer{m_streamId},
                                        utils::RawBuffer{m_finishDataInfoHash},
                                   },
                                  reinterpret_cast<const Signature &>(m_sign) );
        }
    };


}

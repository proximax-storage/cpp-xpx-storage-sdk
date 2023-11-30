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

class EndpointsManager;

    // если размер sandbox-a позволяет, то мы храним все чанки
    // иначе удаляем старые
    //
    struct StreamRequest
    {
        Hash256                     m_streamId;     // transaction hash
        Key                         m_streamerKey;  // streamer public key
        std::string                 m_folder;       // where it will be saved in FsTree
        uint64_t                    m_maxSizeBytes; // could be increased
        ReplicatorList              m_replicatorList;
        
        template <class Archive> void serialize( Archive & arch )
        {
            arch( m_streamId );
            arch( m_streamerKey );
            arch( m_folder );
            arch( m_maxSizeBytes );
            arch( m_replicatorList );
        }
    };

    struct StreamIncreaseRequest
    {
        Hash256                     m_streamId; // transaction hash
        uint64_t                    m_maxSizeBytes; // stream size after increasing

        template <class Archive> void serialize( Archive & arch )
        {
            arch( m_streamId );
            arch( m_maxSizeBytes );
        }
    };

    struct StreamFinishRequest
    {
        Hash256                     m_streamId; // transaction hash
        InfoHash                    m_finishDataInfoHash;
        uint64_t                    m_streamSizeBytes; // stream size after increasing

        template <class Archive> void serialize( Archive & arch )
        {
            arch( m_streamId );
            arch( m_finishDataInfoHash );
            arch( m_streamSizeBytes );
        }
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
    
    struct FinishStreamChunkInfo // it is a record from 'finishStreamInfo' file (on streamer side)
    {
        uint32_t                    m_chunkIndex;
        std::array<uint8_t,32>      m_chunkInfoHash;
        uint32_t                    m_durationMks;   // microseconds
        uint64_t                    m_sizeBytes;
        bool                        m_saveOnDrive = true;

        template <class Archive> void serialize( Archive & arch )
        {
            arch( m_chunkIndex );
            arch( m_chunkInfoHash );
            arch( m_durationMks );
            arch( m_sizeBytes );
            arch( m_saveOnDrive );
        }
    };

//    struct FinishStreamMsg
//    {
//        std::array<uint8_t,32>      m_streamId;
//        std::array<uint8_t,32>      m_finishDataInfoHash;
//        std::array<uint8_t, 64>     m_sign;
//
//        template <class Archive> void serialize( Archive & arch )
//        {
//            arch( m_streamId );
//            arch( m_finishDataInfoHash );
//            arch( m_sign );
//        }
//
//        void Sign( const crypto::KeyPair& keyPair )
//        {
//            crypto::Sign( keyPair,
//                          {
//                                utils::RawBuffer{m_streamId},
//                                utils::RawBuffer{m_finishDataInfoHash},
//                          },
//                         reinterpret_cast<Signature &>(m_sign) );
//        }
//
//        bool Verify( const Key& streamerKey ) const
//        {
//            return crypto::Verify( streamerKey,
//                                   {
//                                        utils::RawBuffer{m_streamId},
//                                        utils::RawBuffer{m_finishDataInfoHash},
//                                   },
//                                  reinterpret_cast<const Signature &>(m_sign) );
//        }
//    };

}

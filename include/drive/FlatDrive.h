/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "types.h"
#include "plugins.h"
#include "crypto/Signer.h"
#include <boost/asio/ip/tcp.hpp>
#include <cereal/archives/binary.hpp>
#include <memory>

namespace sirius { namespace drive {

class FlatDrive;
class Replicator;

    namespace modify_status {
        enum code {
            failed = 0,
            sandbox_root_hash = 1, // calculated in sandbox
            update_completed = 2,
            broken = 3 // terminated
        };
    };

    using DriveModifyHandler = std::function<void( modify_status::code, const FlatDrive& drive, const std::string& error )>;

    struct ModifyRequest {
        InfoHash          m_clientDataInfoHash;
        Hash256           m_transactionHash;
        uint64_t          m_maxDataSize;
        ReplicatorList    m_replicatorList;
        Key               m_clientPublicKey;
    };

    // It is opinition of single replicator about how much data the other peers transferred.
    // (when it downloads modifyData)
    struct SingleOpinion
    {
        // Replicator public key
        std::array<uint8_t,32>  m_replicatorKey;

        // Opinions about how much the Replicators and the Drive Owner have uploaded to this Replicator.
        //TODO
        std::vector<uint8_t>    m_replicatorKeys;
        std::vector<uint64_t>   m_replicatorUploadBytes;
        uint64_t                m_clientUploadBytes = 0;
        
        // Signature of { modifyTransactionHash, rootHash, replicatorsUploadBytes, clientUploadBytes }
        Signature               m_signature;
        
        SingleOpinion() = default;
        
        SingleOpinion( const Key& replicatorKey ) : m_replicatorKey( replicatorKey.array() )
        {
        }
        
        void Sign( const crypto::KeyPair& keyPair, const Hash256& modifyTransactionHash, const InfoHash& rootHash )
        {
            crypto::Sign( keyPair,
                          {
                            utils::RawBuffer{modifyTransactionHash},
                            utils::RawBuffer{rootHash},
                            utils::RawBuffer{ (const uint8_t*) &m_replicatorUploadBytes[0],
                                                m_replicatorUploadBytes.size() * sizeof (m_replicatorUploadBytes[0]) },
                            utils::RawBuffer{(const uint8_t*)&m_clientUploadBytes,sizeof(m_clientUploadBytes)}
                          },
                          m_signature );
        }

        bool Verify( const Key& publicKey, const Hash256& modifyTransactionHash, const InfoHash& rootHash )
        {
            return crypto::Verify( publicKey,
                                  {
                                    utils::RawBuffer{modifyTransactionHash},
                                    utils::RawBuffer{rootHash},
                                    utils::RawBuffer{ (const uint8_t*) &m_replicatorUploadBytes[0],
                                    m_replicatorUploadBytes.size() * sizeof (m_replicatorUploadBytes[0]) },
                                    utils::RawBuffer{(const uint8_t*)&m_clientUploadBytes,sizeof(m_clientUploadBytes)}
                                  },
                                  m_signature );
        }

        template <class Archive> void serialize( Archive & arch ) {
            arch( m_replicatorKey );
            arch( m_replicatorKeys );
            arch( m_replicatorUploadBytes );
            arch( m_clientUploadBytes );
            arch( cereal::binary_data( m_signature.data(), m_signature.size() ) );
        }

    };

    // It is used in 2 cases:
    // - as 'DataModificationApprovalTransaction '
    // - as 'DataModificationSingleApprovalTransaction' (in this case vector 'm_opinions' has single element)
    struct ApprovalTransactionInfo
    {
        // Drive public key
        std::array<uint8_t,32>  m_driveKey;

        // A reference to the transaction that initiated the modification
        std::array<uint8_t,32>  m_modifyTransactionHash;

        // Content Download Information for the File Structure
        std::array<uint8_t,32>  m_rootHash;
        
        // The size of the “File Structure” File
        uint64_t                m_fsTreeFileSize;

        // The size of metafiles (torrents?,folders?) including “File Structure” File
        uint64_t                m_metaFilesSize;

        // Total used disk space. Must not be more than the Drive Size.
        uint64_t                m_driveSize;

        // Opinions about how much the Replicators and the Drive Owner have uploaded to this Replicator.
        std::vector<SingleOpinion>   m_opinions;

        template <class Archive> void serialize( Archive & arch )
        {
            arch( m_driveKey );
            arch( m_modifyTransactionHash );
            arch( m_rootHash );
            arch( m_fsTreeFileSize );
            arch( m_metaFilesSize );
            arch( m_driveSize );
            arch( m_opinions );
        }
    };


    // Iterface for storage extension
    class ReplicatorEventHandler
    {
    public:

        virtual ~ReplicatorEventHandler() = default;

        // It will be called before 'replicator' shuts down
        virtual void willBeTerminated( Replicator& replicator ) = 0;

        // It will be called when transaction could not be completed
        virtual void modifyTransactionIsCanceled( Replicator& replicator,
                                                 const sirius::Key&             driveKey,
                                                 const sirius::drive::InfoHash& modifyTransactionHash,
                                                 const std::string&             reason,
                                                 int                            errorCode ) = 0;
        
        // It will be called when rootHash is calculated in sandbox
        // (TODO it will be removed)
        virtual void rootHashIsCalculated( Replicator&                    replicator,
                                           const sirius::Key&             driveKey,
                                           const sirius::drive::InfoHash& modifyTransactionHash,
                                           const sirius::drive::InfoHash& sandboxRootHash ) = 0;
        
        // It will initiate the approving of modify transaction
        virtual void modifyApproveTransactionIsReady( Replicator& replicator, ApprovalTransactionInfo&& transactionInfo ) = 0;
        
        // It will initiate the approving of single modify transaction
        virtual void singleModifyApproveTransactionIsReady( Replicator& replicator, ApprovalTransactionInfo&& transactionInfo ) = 0;
        
        // It will be called after the drive is syncronized with sandbox
        virtual void driveModificationIsCompleted( Replicator&                    replicator,
                                                   const sirius::Key&             driveKey,
                                                   const sirius::drive::InfoHash& modifyTransactionHash,
                                                   const sirius::drive::InfoHash& rootHash ) = 0;
        
        //TODO
        //virtual DownloadApprovalTransactionInfo& downloadApproveTransactionIsReady() = 0;
    };


    //
    // Drive
    //
    class FlatDrive {
    public:

        virtual ~FlatDrive() = default;
        //virtual void terminate() = 0;

        virtual const Key& drivePublicKey() const = 0;
        
        virtual uint64_t maxSize() const = 0;

        virtual InfoHash rootHash() const = 0;

        virtual uint64_t sandboxFsTreeSize() const = 0;
        
        virtual InfoHash sandboxRootHash() const = 0;

        virtual void     getSandboxDriveSizes( uint64_t& metaFilesSize, uint64_t&  driveSize ) const = 0;

        virtual void     startModifyDrive( ModifyRequest&& modifyRequest ) = 0;

        virtual void     cancelModifyDrive( const Hash256& transactionHash ) = 0;

        virtual void     loadTorrent( const InfoHash& fileHash ) = 0;

        virtual const ModifyRequest& modifyRequest() const = 0;
        
        virtual void     onOpinionReceived( const ApprovalTransactionInfo& anOpinion ) = 0;

        virtual void     onApprovalTransactionReceived( const ApprovalTransactionInfo& transaction ) = 0;

        virtual void     onSingleApprovalTransactionReceived( const ApprovalTransactionInfo& transaction ) = 0;

        // for testing and debugging
        virtual void printDriveStatus() = 0;
    };

    class Session;

    PLUGIN_API std::shared_ptr<FlatDrive> createDefaultFlatDrive( std::shared_ptr<Session> session,
                                                       const std::string&       replicatorRootFolder,
                                                       const std::string&       replicatorSandboxRootFolder,
                                                       const Key&               drivePubKey,
                                                       size_t                   maxSize,
                                                       ReplicatorEventHandler&  eventHandler,
                                                       Replicator&              replicator );
}}


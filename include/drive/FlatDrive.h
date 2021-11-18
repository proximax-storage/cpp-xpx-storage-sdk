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

namespace sirius::drive {

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

    struct DriveRequest {
        uint64_t          driveSize;
        uint64_t          usedDriveSizeExcludingMetafiles;
        ReplicatorList    replicators;
    };

    using DriveModifyHandler = std::function<void( modify_status::code, const FlatDrive& drive, const std::string& error )>;

    struct ModifyRequest {
        InfoHash          m_clientDataInfoHash;
        Hash256           m_transactionHash;
        uint64_t          m_maxDataSize;
        ReplicatorList    m_replicatorList;
        Key               m_clientPublicKey;
        
        bool              m_isCanceled = false;
    };

    struct DownloadRequest {
        Key                  m_channelKey;
        uint64_t             m_prepaidDownloadSize;
        ReplicatorList       m_addrList;
        std::vector<Key>     m_clients;
    };

    // It is opinition of single replicator about how much data the other peers transferred.
    // (when it downloads modifyData)
    struct SingleOpinion
    {
        // Replicator public key
        std::array<uint8_t,32>  m_replicatorKey;

        // Opinions about how much the Replicators and the Drive Owner have uploaded to this Replicator.
        //TODO
        std::vector<uint8_t>    m_uploadReplicatorKeys; // all keys are concatenated into 1 array
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
//            std::cerr <<  "Sign:" << keyPair.publicKey()[0] << "," << modifyTransactionHash[0] << "," << rootHash[0] << "," << m_replicatorUploadBytes[0] <<
//            "," << m_clientUploadBytes << "\n\n";
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

        bool Verify( const Hash256& modifyTransactionHash, const InfoHash& rootHash ) const
        {
//            std::cerr <<  "Verify:" << m_replicatorKey[0] << "," << modifyTransactionHash[0] << "," << rootHash[0] << "," << m_replicatorUploadBytes[0] <<
//            "," << m_clientUploadBytes << "\n\n";
            return crypto::Verify( m_replicatorKey,
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
            arch( m_uploadReplicatorKeys );
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

        // New root hash (hash of the File Structure)
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

    struct DownloadOpinion
    {
        // Replicator public key
        std::array<uint8_t,32>  m_replicatorKey;

        std::vector<uint64_t>   m_downloadedBytes;
        
        // Signature of { modifyTransactionHash, rootHash, replicatorsUploadBytes, clientUploadBytes }
        Signature               m_signature;
        
        DownloadOpinion() = default;
        
        DownloadOpinion( const Key& replicatorKey ) : m_replicatorKey( replicatorKey.array() )
        {
        }
        
        void Sign( const crypto::KeyPair& keyPair, const std::array<uint8_t,32>& blockHash, const std::array<uint8_t,32>& downloadChannelId )
        {
            crypto::Sign( keyPair,
                          {
                            utils::RawBuffer{blockHash},
                            utils::RawBuffer{downloadChannelId},
                            utils::RawBuffer{ (const uint8_t*) &m_downloadedBytes[0],
                                m_downloadedBytes.size() * sizeof (m_downloadedBytes[0]) },
                          },
                          m_signature );
        }

        bool Verify( const std::array<uint8_t,32>& blockHash, const std::array<uint8_t,32>& downloadChannelId ) const
        {
            return crypto::Verify( m_replicatorKey,
                                   {
                                        utils::RawBuffer{blockHash},
                                        utils::RawBuffer{downloadChannelId},
                                        utils::RawBuffer{ (const uint8_t*) &m_downloadedBytes[0],
                                        m_downloadedBytes.size() * sizeof (m_downloadedBytes[0]) },
                                   },
                                   m_signature );
        }

        template <class Archive> void serialize( Archive & arch ) {
            arch( m_replicatorKey );
            arch( m_downloadedBytes );
            arch( cereal::binary_data( m_signature.data(), m_signature.size() ) );
        }
    };

    struct DownloadApprovalTransactionInfo
    {
        // Its id
        std::array<uint8_t,32>  m_blockHash;
        
        // Transaction hash
        std::array<uint8_t,32>  m_downloadChannelId;

        //
        uint32_t                m_replicatorNumber;

        // Opinions about how much the Replicators and the Drive Owner have uploaded to this Replicator.
        std::vector<DownloadOpinion>   m_opinions;

        template <class Archive> void serialize( Archive & arch )
        {
            arch( m_blockHash );
            arch( m_downloadChannelId );
            arch( m_replicatorNumber );
            arch( m_opinions );
        }
    };


    // Iterface for storage extension
    class ReplicatorEventHandler
    {
    public:

        virtual ~ReplicatorEventHandler() = default;

        // It will be called when modification ended with error (for example small disc space)
        virtual void modifyTransactionEndedWithError( Replicator&               replicator,
                                                     const sirius::Key&         driveKey,
                                                     const ModifyRequest&       modifyRequest,
                                                     const std::string&         reason,
                                                     int                        errorCode ) = 0;
        
        // It will initiate the approving of modify transaction
        virtual void modifyApprovalTransactionIsReady( Replicator& replicator, ApprovalTransactionInfo&& transactionInfo ) = 0;
        
        // It will initiate the approving of single modify transaction
        virtual void singleModifyApprovalTransactionIsReady( Replicator& replicator, ApprovalTransactionInfo&& transactionInfo ) = 0;
        
        // It will be called when transaction could not be completed
        virtual void downloadApprovalTransactionIsReady( Replicator& replicator, const DownloadApprovalTransactionInfo& ) = 0;

        // It will be called in response on CancelModifyTransaction
        virtual void driveModificationIsCanceled(  Replicator&                  replicator,
                                                   const sirius::Key&           driveKey,
                                                   const Hash256&               modifyTransactionHash )
        {
        }

        // It will be called in response on CloseDriveTransaction
        // It is needed to remove 'drive' from drive list (by Storage Extension)
        // (If this method has not been not called, then the disk has not yet been removed from the HDD - operation is not comapleted)
        virtual void driveIsClosed(  Replicator&                replicator,
                                     const sirius::Key&         driveKey,
                                     const Hash256&             transactionHash )
        {
            //todo make it pure virtual function?
        }
    };

    class DbgReplicatorEventHandler
    {
    public:

        virtual ~DbgReplicatorEventHandler() = default;

        // It will be called after the drive is syncronized with sandbox
        virtual void driveModificationIsCompleted( Replicator&                    replicator,
                                                   const sirius::Key&             driveKey,
                                                   const Hash256&                 modifyTransactionHash,
                                                   const sirius::drive::InfoHash& rootHash )
        {
            // for debugging?
        }

        // It will be called when rootHash is calculated in sandbox
        // (TODO it will be removed)
        virtual void rootHashIsCalculated( Replicator&                    replicator,
                                           const sirius::Key&             driveKey,
                                           const Hash256&                 modifyTransactionHash,
                                           const sirius::drive::InfoHash& sandboxRootHash )
        {
            // for debugging?
        }
        
        // It will be called before 'replicator' shuts down
        virtual void willBeTerminated( Replicator& replicator )
        {
            //?
        }
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

        virtual ReplicatorList getReplicators() = 0;

        virtual void updateReplicators(const ReplicatorList& replicators) = 0;

        virtual void     getSandboxDriveSizes( uint64_t& metaFilesSize, uint64_t&  driveSize ) const = 0;

        virtual void     startModifyDrive( ModifyRequest&& modifyRequest ) = 0;

        virtual void     cancelModifyDrive( const Hash256& transactionHash ) = 0;

        virtual void     startDriveClosing( const Hash256& transactionHash ) = 0;

        virtual void     loadTorrent( const InfoHash& fileHash ) = 0;
        
//        virtual void     onDownloadOpinionReceived( const DownloadApprovalTransactionInfo& anOpinion ) = 0;
//
//        virtual void     prepareDownloadApprovalTransactionInfo() = 0;
        
//        virtual const ModifyRequest& modifyRequest() const = 0;
        
        virtual void     onOpinionReceived( const ApprovalTransactionInfo& anOpinion ) = 0;

        virtual void     onApprovalTransactionHasBeenPublished( const ApprovalTransactionInfo& transaction ) = 0;

        virtual void     onSingleApprovalTransactionHasBeenPublished( const ApprovalTransactionInfo& transaction ) = 0;
        
        // It will be called by replicator
        virtual const std::optional<Hash256>& closingTxHash() const = 0;
        
        virtual void removeAllDriveData() = 0;

        virtual const ReplicatorList&  replicatorList() const = 0;

        // for testing and debugging
        virtual void printDriveStatus() = 0;
    };

    class Session;

    PLUGIN_API std::shared_ptr<FlatDrive> createDefaultFlatDrive( std::shared_ptr<Session> session,
                                                       const std::string&       replicatorRootFolder,
                                                       const std::string&       replicatorSandboxRootFolder,
                                                       const Key&               drivePubKey,
                                                       size_t                   maxSize,
                                                       size_t                   usedDriveSizeExcludingMetafiles,
                                                       ReplicatorEventHandler&  eventHandler,
                                                       Replicator&              replicator,
                                                       const ReplicatorList&    replicators,
                                                       DbgReplicatorEventHandler* dbgEventHandler = nullptr );
}


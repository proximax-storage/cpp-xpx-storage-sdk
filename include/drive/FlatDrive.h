/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "types.h"
#include "log.h"
#include "plugins.h"
#include "ModificationStatus.h"
#include "crypto/Signer.h"
#include "drive/Streaming.h"
#include <libtorrent/alert_types.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>
#include <cereal/archives/binary.hpp>
#include <cereal/types/set.hpp>
#include <memory>
#include "drive/ManualModificationsRequests.h"

namespace sirius::drive
{

class FlatDrive;

class Replicator;

    enum class DriveTaskType
    {
        DRIVE_INITIALIZATION,
        DRIVE_CLOSURE,
        MODIFICATION_CANCEL,
        CATCHING_UP,
        MODIFICATION_REQUEST,
        STREAM_REQUEST,
        DRIVE_VERIFICATION,
        MANUAL_MODIFICATION,
        MANUAL_SYNCHRONIZATION
    };

    inline std::string driveTaskTypeToString( DriveTaskType t )
    {
        switch(t)
        {
            case DriveTaskType::DRIVE_INITIALIZATION:       return "initialization";
            case DriveTaskType::DRIVE_CLOSURE:              return "drive_closure";
            case DriveTaskType::MODIFICATION_CANCEL:        return "modification_cancel";
            case DriveTaskType::CATCHING_UP:                return "catching_up";
            case DriveTaskType::MODIFICATION_REQUEST:       return "modification";
            case DriveTaskType::STREAM_REQUEST:             return "stream";
            case DriveTaskType::DRIVE_VERIFICATION:         return "verification";
            case DriveTaskType::MANUAL_MODIFICATION:        return "manual modification";
            case DriveTaskType::MANUAL_SYNCHRONIZATION:     return "manual synchronization";
        };
        return "unknown";
    }

    namespace modify_status {
        enum code {
    failed = 0,
    sandbox_root_hash = 1, // calculated in sandbox
    update_completed = 2,
    broken = 3 // terminated
};
};

struct CompletedModification
{

    enum class CompletedModificationStatus
    {
        APPROVED, CANCELLED
    };

    Hash256 m_modificationId;
    CompletedModificationStatus m_completedModificationStatus;

    template<class Archive>
    void serialize( Archive& arch )
    {
        arch( m_modificationId );
        arch( m_completedModificationStatus );
    }
};

struct AddDriveRequest
{
    uint64_t m_driveSize;
    uint64_t m_expectedCumulativeDownloadSize;
    std::vector<CompletedModification> m_completedModifications;
    ReplicatorList m_fullReplicatorList;
    Key m_client;
    ReplicatorList m_modifyDonatorShard;
    ReplicatorList m_modifyRecipientShard;

    template<class Archive>
    void serialize( Archive& arch )
    {
        arch( m_driveSize );
        arch( m_expectedCumulativeDownloadSize );
        arch( m_completedModifications );
        arch( m_fullReplicatorList );
        arch( m_client );
        arch( m_modifyDonatorShard );
        arch( m_modifyRecipientShard );
    }

};

using DriveModifyHandler = std::function<void( modify_status::code, const FlatDrive& drive, const std::string& error )>;

struct ModificationRequest
{
    InfoHash m_clientDataInfoHash;
    Hash256 m_transactionHash;
    uint64_t m_maxDataSize;
    ReplicatorList m_replicatorList;

    bool m_isCanceled = false;

    template<class Archive>
    void serialize( Archive& arch )
    {
        arch( m_clientDataInfoHash );
        arch( m_transactionHash );
        arch( m_maxDataSize );
        arch( m_replicatorList );
        arch( m_isCanceled );
    }
};

struct CatchingUpRequest
{
        CatchingUpRequest( const InfoHash& rootHash, const Hash256& modifyTransactionHash ) : m_rootHash(rootHash), m_modifyTransactionHash(modifyTransactionHash) {}

    InfoHash m_rootHash;
    Hash256 m_modifyTransactionHash;
};

struct ModificationCancelRequest
{
        ModificationCancelRequest( const Hash256& modifyTransactionHash ) : m_modifyTransactionHash(modifyTransactionHash) {}
    Hash256 m_modifyTransactionHash;
};

struct DriveClosureRequest
{
        DriveClosureRequest() = default;
        DriveClosureRequest( const Hash256& removeDriveTx ) : m_removeDriveTx(removeDriveTx) {}
    std::optional<Hash256> m_removeDriveTx;
};

struct DownloadRequest
{
    Key m_channelKey;
    uint64_t m_prepaidDownloadSize;
    std::vector<Key> m_replicators;
    std::vector<Key> m_clients;

    template<class Archive>
    void serialize( Archive& arch )
    {
        arch( m_channelKey );
        arch( m_prepaidDownloadSize );
        arch( m_replicators );
        arch( m_clients );
    }
};

struct KeyAndBytes
{
    std::array<uint8_t, 32> m_key;
    uint64_t m_uploadedBytes;

    template<class Archive>
    void serialize( Archive& arch )
    {
        arch( m_key );
        arch( m_uploadedBytes );
    }
};

// It is opinition of single replicator about how much data the other peers transferred.
// (when it downloads modifyData)
struct SingleOpinion
{
    // Replicator public key
    std::array<uint8_t, 32> m_replicatorKey;

    std::vector<KeyAndBytes> m_uploadLayout;

    // Signature of { modifyTransactionHash, rootHash, replicatorsUploadBytes, clientUploadBytes }
    Signature m_signature;

    SingleOpinion() = default;

    SingleOpinion( const Key& replicatorKey )
            : m_replicatorKey( replicatorKey.array())
    {
    }

    void Sign( const crypto::KeyPair& keyPair,
               const Key& driveKey,
               const Hash256& modifyTransactionHash,
               const InfoHash& rootHash,
				   ModificationStatus status,
                   uint64_t fsTreeFileSize,
                   uint64_t metaFilesSize,
                   uint64_t driveSize)
    {
//            std::cerr <<  "Sign:" << keyPair.publicKey()[0] << "," << modifyTransactionHash[0] << "," << rootHash[0] << "," << m_replicatorUploadBytes[0] <<
//            "," << m_clientUploadBytes << "\n\n";
        crypto::Sign( keyPair,
                      {
                              utils::RawBuffer{driveKey},
                              utils::RawBuffer{modifyTransactionHash},
                              utils::RawBuffer{rootHash},
                            utils::RawBuffer{(const uint8_t*) &status, sizeof(status)},
                              utils::RawBuffer{(const uint8_t*) &fsTreeFileSize, sizeof( fsTreeFileSize )},
                              utils::RawBuffer{(const uint8_t*) &metaFilesSize, sizeof( metaFilesSize )},
                              utils::RawBuffer{(const uint8_t*) &driveSize, sizeof( driveSize )},
                              utils::RawBuffer{(const uint8_t*) &m_uploadLayout[0],
                                               m_uploadLayout.size() * sizeof( m_uploadLayout[0] )}
                      },
                      m_signature );
    }

    bool
    Verify( const crypto::KeyPair& keyPair,
            const Key& driveKey,
            const Hash256& modifyTransactionHash,
            const InfoHash& rootHash,
					   ModificationStatus status,
					   uint64_t fsTreeFileSize,
					   uint64_t metaFilesSize,
					   uint64_t driveSize) const {
        //            std::cerr <<  "Verify:" << m_replicatorKey[0] << "," << modifyTransactionHash[0] << "," <<
        //            rootHash[0] << "," << m_replicatorUploadBytes[0] <<
        //            "," << m_clientUploadBytes << "\n\n";
        return crypto::Verify(
                m_replicatorKey,
                {utils::RawBuffer{driveKey},
                 utils::RawBuffer{modifyTransactionHash},
                 utils::RawBuffer{rootHash},
					  utils::RawBuffer { (const uint8_t*)&status, sizeof(status) },
                 utils::RawBuffer{(const uint8_t*) &fsTreeFileSize, sizeof( fsTreeFileSize )},
                 utils::RawBuffer{(const uint8_t*) &metaFilesSize, sizeof( metaFilesSize )},
                 utils::RawBuffer{(const uint8_t*) &driveSize, sizeof( driveSize )},

                 utils::RawBuffer{(const uint8_t*) &m_uploadLayout[0],
                                  m_uploadLayout.size() * sizeof( m_uploadLayout[0] )}},
                m_signature );
    }

    template<class Archive>
    void serialize( Archive& arch )
    {
        arch( m_replicatorKey );
        arch( m_uploadLayout );
        arch( cereal::binary_data( m_signature.data(), m_signature.size()));
    }

};

struct PublishedModificationApprovalTransactionInfo
{
    // Drive public key
    std::array<uint8_t, 32> m_driveKey;

    // A reference to the transaction that initiated the modification
    std::array<uint8_t, 32> m_modifyTransactionHash;

    // New root hash (hash of the File Structure)
    std::array<uint8_t, 32> m_rootHash;

    // Keys of the cosigners
    std::vector<std::array<uint8_t, 32>> m_replicatorKeys;

    template<class Archive>
    void serialize( Archive& arch )
    {
        arch( m_driveKey );
        arch( m_modifyTransactionHash );
        arch( m_rootHash );
        arch( m_replicatorKeys );
    }
};

struct PublishedModificationSingleApprovalTransactionInfo
{
    // Drive public key
    std::array<uint8_t, 32> m_driveKey;

    // A reference to the transaction that initiated the modification
    std::array<uint8_t, 32> m_modifyTransactionHash;

    template<class Archive>
    void serialize( Archive& arch )
    {
        arch( m_driveKey );
        arch( m_modifyTransactionHash );
    }
};

struct PublishedVerificationApprovalTransactionInfo
{
    // requested tx
    std::array<uint8_t, 32> m_tx;

    // Drive public key
    std::array<uint8_t, 32> m_driveKey;

    template<class Archive>
    void serialize( Archive& arch )
    {
        arch( m_tx );
        arch( m_driveKey );
    }
};

// It is used in 2 cases:
// - as 'DataModificationApprovalTransaction '
// - as 'DataModificationSingleApprovalTransaction' (in this case vector 'm_opinions' has single element)
struct ApprovalTransactionInfo
{
    // Drive public key
    std::array<uint8_t, 32> m_driveKey;

    // A reference to the transaction that initiated the modification
    std::array<uint8_t, 32> m_modifyTransactionHash;

    // New root hash (hash of the File Structure)
    std::array<uint8_t, 32> m_rootHash;

		// Modification status
		ModificationStatus 		m_status;

    // The size of the “File Structure” File
    uint64_t m_fsTreeFileSize;

    // The size of metafiles (torrents?,folders?) including “File Structure” File
    uint64_t m_metaFilesSize;

    // Total used disk space. Must not be more than the Drive Size.
    uint64_t m_driveSize;

    // Opinions about how much the Replicators and the Drive Owner have uploaded to this Replicator.
    std::vector<SingleOpinion> m_opinions;

    template<class Archive>
    void serialize( Archive& arch )
    {
        arch( m_driveKey );
        arch( m_modifyTransactionHash );
        arch( m_rootHash );
			arch( m_status );
        arch( m_fsTreeFileSize );
        arch( m_metaFilesSize );
        arch( m_driveSize );
        arch( m_opinions );
    }

    operator PublishedModificationApprovalTransactionInfo() const
    {
        std::vector<std::array<uint8_t, 32>> replicatorKeys;
        for ( const auto& opinion: m_opinions )
        {
            replicatorKeys.push_back( opinion.m_replicatorKey );
        }
        return {m_driveKey, m_modifyTransactionHash, m_rootHash, replicatorKeys};
    }

    operator PublishedModificationSingleApprovalTransactionInfo() const
    {
        return {m_driveKey, m_modifyTransactionHash};
    }
};

struct DownloadOpinion
{
    // Replicator public key
    std::array<uint8_t, 32> m_replicatorKey;

    std::vector<KeyAndBytes> m_downloadLayout;

    // Signature of { modifyTransactionHash, rootHash, replicatorsUploadBytes, clientUploadBytes }
    Signature m_signature;

    DownloadOpinion() = default;

    DownloadOpinion( const Key& replicatorKey )
            : m_replicatorKey( replicatorKey.array())
    {
    }

    void Sign( const crypto::KeyPair& keyPair,
               const std::array<uint8_t, 32>& blockHash,
               const std::array<uint8_t, 32>& downloadChannelId )
    {
        crypto::Sign( keyPair,
                      {
                              utils::RawBuffer{downloadChannelId},
                              utils::RawBuffer{blockHash},
                              utils::RawBuffer{(const uint8_t*) &m_downloadLayout[0],
                                               m_downloadLayout.size() * sizeof( m_downloadLayout[0] )},
                      },
                      m_signature );
    }

    bool Verify( const std::array<uint8_t, 32>& blockHash, const std::array<uint8_t, 32>& downloadChannelId ) const
    {
        return crypto::Verify( m_replicatorKey,
                               {
                                       utils::RawBuffer{downloadChannelId},
                                       utils::RawBuffer{blockHash},
                                       utils::RawBuffer{(const uint8_t*) &m_downloadLayout[0],
                                                        m_downloadLayout.size() * sizeof( m_downloadLayout[0] )},
                               },
                               m_signature );
    }

    template<class Archive>
    void serialize( Archive& arch )
    {
        arch( m_replicatorKey );
        arch( m_downloadLayout );
        arch( cereal::binary_data( m_signature.data(), m_signature.size()));
    }
};

struct DownloadApprovalTransactionInfo
{
        DownloadApprovalTransactionInfo() = default;
        DownloadApprovalTransactionInfo( const std::array<uint8_t,32>&        blockHash,
                                         const std::array<uint8_t,32>&        downloadChannelId,
                                         const std::vector<DownloadOpinion>&  opinions )
        :
            m_blockHash(blockHash),
            m_downloadChannelId(downloadChannelId),
            m_opinions(opinions)
        {}

    // Its id
    std::array<uint8_t, 32> m_blockHash;

    // Transaction hash
    std::array<uint8_t, 32> m_downloadChannelId;

    // Opinions about how much the Replicators and the Drive Owner have uploaded to this Replicator.
    std::vector<DownloadOpinion> m_opinions;

    template<class Archive>
    void serialize( Archive& arch )
    {
        arch( m_blockHash );
        arch( m_downloadChannelId );
        arch( m_opinions );
    }
};

struct VerificationRequest
{
    Hash256 m_tx;
    uint16_t m_shardId = 0;
    Hash256 m_approvedModification;
    std::vector<Key> m_replicators;
    std::uint32_t m_durationMs;
    std::set<Key> m_blockedReplicators; // blocked until verification will be approved

    template<class Archive>
    void serialize( Archive& arch )
    {
        arch( m_tx );
        arch( m_shardId );
        arch( m_approvedModification );
        arch( m_replicators );
        arch( m_durationMs );
        arch( m_blockedReplicators );
    }
};

struct VerificationCodeInfo
{
    std::array<uint8_t, 32> m_tx;
    std::array<uint8_t, 32> m_replicatorKey;
    std::array<uint8_t, 32> m_driveKey;
    uint64_t m_code;

    Signature m_signature;

    template<class Archive>
    void serialize( Archive& arch )
    {
        arch( m_tx );
        arch( m_replicatorKey );
        arch( m_driveKey );
        arch( m_code );
        arch( cereal::binary_data( m_signature.data(), m_signature.size()));
    }

    void Sign( const crypto::KeyPair& keyPair )
    {
        crypto::Sign( keyPair,
                      {
                              utils::RawBuffer{m_tx},
                              utils::RawBuffer{m_driveKey},
                              utils::RawBuffer{(const uint8_t*) &m_code, sizeof( m_code )},
                      },
                      m_signature );
    }

    bool Verify() const
    {
        return crypto::Verify( m_replicatorKey,
                               {
                                       utils::RawBuffer{m_tx},
                                       utils::RawBuffer{m_driveKey},
                                       utils::RawBuffer{(const uint8_t*) &m_code, sizeof( m_code )},
                               },
                               m_signature );
    }
};

struct VerifyOpinion
{
    std::array<uint8_t, 32> m_publicKey;
    std::vector<uint8_t> m_opinions;

    // our publicKey, m_tx, m_driveKey, m_shardId, m_opinions
    Signature m_signature;

    template<class Archive>
    void serialize( Archive& arch )
    {
        arch( m_publicKey );
        arch( m_opinions );
        arch( cereal::binary_data( m_signature.data(), m_signature.size()));
    }

    void Sign( const crypto::KeyPair& keyPair,
               const std::array<uint8_t, 32>& tx,
               const std::array<uint8_t, 32>& driveKey,
               uint16_t shardId )
    {
        crypto::Sign( keyPair,
                      {
                              utils::RawBuffer{driveKey},
                              utils::RawBuffer{tx},
                              utils::RawBuffer{(const uint8_t*) &shardId, sizeof( shardId )},
                              utils::RawBuffer{m_opinions},
                      },
                      m_signature );
    }

    bool Verify( const std::array<uint8_t, 32>& tx,
                 const std::array<uint8_t, 32>& driveKey,
                 uint16_t shardId ) const
    {
        return crypto::Verify( m_publicKey,
                               {
                                       utils::RawBuffer{driveKey},
                                       utils::RawBuffer{tx},
                                       utils::RawBuffer{(const uint8_t*) &shardId, sizeof( shardId )},
                                       utils::RawBuffer{m_opinions},
                               },
                               m_signature );
    }
};

struct VerifyApprovalTxInfo
{
    std::array<uint8_t, 32> m_tx;
    std::array<uint8_t, 32> m_driveKey;
    uint16_t m_shardId = 0;
    std::vector<VerifyOpinion> m_opinions;

    template<class Archive>
    void serialize( Archive& arch )
    {
        arch( m_tx );
        arch( m_driveKey );
        arch( m_shardId );
        arch( m_opinions );
    }

    operator PublishedVerificationApprovalTransactionInfo() const
    {
        return {m_tx, m_driveKey};
    }
};

// Interface for storage extension
class ReplicatorEventHandler
{
public:

    virtual ~ReplicatorEventHandler() = default;

    virtual void verificationTransactionIsReady( Replicator& replicator,
                                                 const VerifyApprovalTxInfo& transactionInfo
    )
    {
    }

    //
    // TODO: replace 'ApprovalTransactionInfo&& transactionInfo' by 'const ApprovalTransactionInfo& transactionInfo'
    // (also VerifyApprovalInfo)

    // It will initiate the approving of modify transaction
    virtual void
    modifyApprovalTransactionIsReady( Replicator& replicator, const ApprovalTransactionInfo& transactionInfo ) = 0;

    // It will initiate the approving of single modify transaction
    virtual void singleModifyApprovalTransactionIsReady( Replicator& replicator,
                                                         const ApprovalTransactionInfo& transactionInfo ) = 0;

    // It will be called when transaction could not be completed
    virtual void
    downloadApprovalTransactionIsReady( Replicator& replicator, const DownloadApprovalTransactionInfo& ) = 0;

    virtual void opinionHasBeenReceived( Replicator& replicator,
                                         const ApprovalTransactionInfo& ) = 0;

    virtual void downloadOpinionHasBeenReceived( Replicator& replicator,
                                                 const DownloadApprovalTransactionInfo& ) = 0;

    virtual void onLibtorrentSessionError( const std::string& message )
    {
        _LOG_ERR( "onLibtorrentSessionError: " << message );
    }
};

class DbgReplicatorEventHandler
{
public:

    virtual ~DbgReplicatorEventHandler() = default;

    // It will be called after the drive is syncronized with sandbox
    virtual void driveModificationIsCompleted( Replicator& replicator,
                                               const sirius::Key& driveKey,
                                               const Hash256& modifyTransactionHash,
                                               const sirius::drive::InfoHash& rootHash )
    {
        // for debugging?
    }

    // It will be called when rootHash is calculated in sandbox
    virtual void rootHashIsCalculated( Replicator& replicator,
                                       const sirius::Key& driveKey,
                                       const Hash256& modifyTransactionHash,
                                       const sirius::drive::InfoHash& sandboxRootHash )
    {
        // for debugging?
    }

    // It will be called before 'replicator' shuts down
    virtual void willBeTerminated( Replicator& replicator )
    {
        //?
    }

    // It will be called after drive initializing
    virtual void driveAdded( const sirius::Key& driveKey )
    {
    }

    // It will be called when rootHash is calculated in sandbox
    virtual void driveIsInitialized( Replicator& replicator,
                                     const sirius::Key& driveKey,
                                     const sirius::drive::InfoHash& rootHash )
    {
    }

    // It will be called in response on CloseDriveTransaction
    // It is needed to remove 'drive' from drive list (by Storage Extension)
    // (If this method has not been not called, then the disk has not yet been removed from the HDD - operation is not comapleted)
    virtual void driveIsClosed( Replicator& replicator,
                                const sirius::Key& driveKey,
                                const Hash256& transactionHash )
    {
    }

    virtual void driveIsRemoved( Replicator& replicator,
                                 const sirius::Key& driveKey )
    {
    }

    // It will be called in response on CancelModifyTransaction
    virtual void driveModificationIsCanceled( Replicator& replicator,
                                              const sirius::Key& driveKey,
                                              const Hash256& modifyTransactionHash )
    {
    }

    // It will be called when modification ended with error (for example small disc space)
    virtual void modifyTransactionEndedWithError( Replicator& replicator,
                                                  const sirius::Key& driveKey,
                                                  const ModificationRequest& modifyRequest,
                                                  const std::string& reason,
                                                  int errorCode ) = 0;
};

// It is used for mutual calculation of the replicators, when they download 'modify data'
// (Note. Replicators could receive 'modify data' from client and from replicators, that already receives some piece)
struct ModifyTraffic
{
    // It is the size received from another replicator or client
    uint64_t m_receivedSize = 0;

    // It is the size sent to another replicator
    uint64_t m_requestedSize = 0;

    template <class Archive> void serialize( Archive & arch )
    {
        arch( m_receivedSize );
        arch( m_requestedSize );
    }
};


// The key is replicator key
using ModifyTrafficMap = std::map<std::array<uint8_t,32>,ModifyTraffic>;

struct ModifyTrafficInfo
{
    std::array<uint8_t,32>  m_driveKey;
    ModifyTrafficMap        m_modifyTrafficMap;

    template <class Archive> void serialize( Archive & arch )
    {
        arch( m_driveKey );
        arch( m_modifyTrafficMap );
    }
};

//
// Drive
//
class FlatDrive
{

public:

    virtual ~FlatDrive() = default;

    virtual void shutdown() = 0;

    virtual const Key& drivePublicKey() const = 0;

    virtual uint64_t maxSize() const = 0;

    virtual InfoHash rootHash() const = 0;

    virtual const ReplicatorList& getAllReplicators() const = 0;

    virtual const Key& driveOwner() const = 0;

    virtual void setReplicators( mobj<ReplicatorList>&& replicatorKeys ) = 0;

    virtual void startModifyDrive( mobj<ModificationRequest>&& modifyRequest ) = 0;

    virtual void cancelModifyDrive( mobj<ModificationCancelRequest>&& request ) = 0;

    // current modification info
    virtual ModifyTrafficInfo&                currentModifyInfo() = 0;
    virtual const std::optional<Hash256>      currentModifyTx() = 0;
    virtual void                              resetCurrentModifyInfo() = 0;

    virtual const ModifyTrafficInfo*          findModifyInfo( const Hash256& tx, bool& outIsFinished ) = 0;

    virtual void initiateManualModifications( mobj<InitiateModificationsRequest>&& request ) = 0;

    virtual void initiateManualSandboxModifications( mobj<InitiateSandboxModificationsRequest>&& request ) = 0;

    virtual void openFile( mobj<OpenFileRequest>&& request ) = 0;

    virtual void writeFile( mobj<WriteFileRequest>&& request ) = 0;

    virtual void readFile( mobj<ReadFileRequest>&& request ) = 0;

    virtual void flush( mobj<FlushRequest>&& request ) = 0;

    virtual void closeFile( mobj<CloseFileRequest>&& request ) = 0;

    virtual void removeFsTreeEntry( mobj<RemoveFilesystemEntryRequest>&& request ) = 0;

    virtual void pathExist( mobj<PathExistRequest>&& request ) = 0;

    virtual void pathIsFile( mobj<PathIsFileRequest>&& request ) = 0;

    virtual void fileSize( mobj<FileSizeRequest>&& response ) = 0;

    virtual void createDirectories( mobj<CreateDirectoriesRequest>&& request ) = 0;

    virtual void folderIteratorCreate( mobj<FolderIteratorCreateRequest>&& request ) = 0;

    virtual void folderIteratorDestroy( mobj<FolderIteratorDestroyRequest>&& request )= 0;

    virtual void folderIteratorHasNext( mobj<FolderIteratorHasNextRequest>&& request ) = 0;

    virtual void folderIteratorNext( mobj<FolderIteratorNextRequest>&& request ) = 0;

    virtual void moveFsTreeEntry( mobj<MoveFilesystemEntryRequest>&& ) = 0;

    virtual void applySandboxManualModifications( mobj<ApplySandboxModificationsRequest>&& request ) = 0;

    virtual void evaluateStorageHash( mobj<EvaluateStorageHashRequest>&& request ) = 0;

    virtual void applyStorageManualModifications( mobj<ApplyStorageModificationsRequest>&& request ) = 0;

    virtual void manualSynchronize( mobj<SynchronizationRequest>&& request ) = 0;

    virtual void getFileInfo( mobj<FileInfoRequest>&& request ) = 0;

    virtual void getActualModificationId( mobj<ActualModificationIdRequest>&& request ) = 0;

    virtual void getFilesystem( const FilesystemRequest& request ) = 0;

    virtual void startDriveClosing( mobj<DriveClosureRequest>&& request ) = 0;

    virtual void startVerification( mobj<VerificationRequest>&& request ) = 0;

    virtual void cancelVerification() = 0;

    virtual void startStream( mobj<StreamRequest>&& ) = 0;

    virtual void increaseStream( mobj<StreamIncreaseRequest>&& ) = 0;

    virtual void finishStream( mobj<StreamFinishRequest>&& ) = 0;

    // modification shards
    virtual void setShardDonator( mobj<ReplicatorList>&& replicatorKeys ) = 0;

    virtual void setShardRecipient( mobj<ReplicatorList>&& replicatorKeys ) = 0;

    virtual const ReplicatorList& donatorShard() const = 0;

    virtual bool acceptConnectionFromReplicator( const Key& ) const = 0;

    virtual bool acceptConnectionFromClient( const Key&, const Hash256& ) const = 0;

    virtual void onVerifyApprovalTransactionHasBeenPublished( PublishedVerificationApprovalTransactionInfo info ) = 0;

    virtual void onVerificationCodeReceived( mobj<VerificationCodeInfo>&& info ) = 0;

    virtual void onVerificationOpinionReceived( mobj<VerifyApprovalTxInfo>&& info ) = 0;

//        virtual void     loadTorrent( const InfoHash& fileHash ) = 0;

    virtual void onOpinionReceived( mobj<ApprovalTransactionInfo>&& anOpinion ) = 0;

    virtual void
    onApprovalTransactionHasBeenPublished( const PublishedModificationApprovalTransactionInfo& transaction ) = 0;

    virtual void onApprovalTransactionHasFailedInvalidOpinions( const Hash256& transactionHash ) = 0;

    virtual void onSingleApprovalTransactionHasBeenPublished(
            const PublishedModificationSingleApprovalTransactionInfo& transaction ) = 0;

    // actualRootHash should not be empty if it is called from replicator::asyncAddDrive()
    //virtual void     startCatchingUp( std::optional<CatchingUpRequest>&& actualRootHash ) = 0;

    // for testing and debugging
    virtual void dbgPrintDriveStatus() = 0;

    static std::string driveIsClosingPath( const std::string& driveRootPath );

    virtual void acceptChunkInfoMessage( mobj<ChunkInfo>&&, const boost::asio::ip::udp::endpoint& sender ) = 0;

    virtual void acceptFinishStreamTx( mobj<StreamFinishRequest>&& ) = 0;

    virtual std::string acceptGetChunksInfoMessage( const std::array<uint8_t, 32>& streamId,
                                                    uint32_t chunkIndex,
                                                    const boost::asio::ip::udp::endpoint& viewer ) = 0;

    virtual std::string acceptGetPlaylistHashRequest( const std::array<uint8_t, 32>& streamId ) = 0;

    virtual std::optional<DriveTaskType> getDriveStatus( const std::array<uint8_t,32>& interectedTaskTx, bool& outIsTaskQueued ) = 0;

    virtual std::string getStreamStatus() = 0;
    
    virtual void dbgTestKademlia2( ReplicatorList& outReplicatorList ) {}

};

class Session;

PLUGIN_API std::shared_ptr<FlatDrive> createDefaultFlatDrive(
        std::shared_ptr<Session> session,
        const std::string& replicatorRootFolder,
        const std::string& replicatorSandboxRootFolder,
        const Key& drivePubKey,
        const Key& clientPubKey,
        size_t maxSize,
        size_t expectedCumulativeDownload,
        std::vector<CompletedModification>&& completedModifications,
        ReplicatorEventHandler& eventHandler,
        Replicator& replicator,
        const ReplicatorList& fullReplicatorList,
        const ReplicatorList& modifyDonatorShard,
        const ReplicatorList& modifyRecipientShard,
        DbgReplicatorEventHandler* dbgEventHandler = nullptr );
}


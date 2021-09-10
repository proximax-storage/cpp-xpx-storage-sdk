/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include "types.h"
#include "plugins.h"
#include "drive/FlatDrive.h"
#include "crypto/Signer.h"
#include "Session.h"


namespace sirius::drive {

struct SingleApprovalTransactionInfo
{
    SingleApprovalTransactionInfo( const Key& replicatorKey ) : m_replicatorKey( replicatorKey )
    {
    }
    
    // Replicator public key
    Key                     m_replicatorKey;

    // Opinions about how much the Replicators and the Drive Owner have uploaded to this Replicator.
    std::vector<uint64_t>   m_replicatorsUploadBytes;
    uint64_t                m_clientUploadBytes = 0;
    
    // Signature of { modifyTransactionHash, rootHash, replicatorsUploadBytes, clientUploadBytes }
    Signature               m_signature;
    
    void Sign( const crypto::KeyPair& keyPair, const Hash256& modifyTransactionHash, const InfoHash& rootHash )
    {
        crypto::Sign( keyPair,
                      {
                        utils::RawBuffer{modifyTransactionHash},
                        utils::RawBuffer{rootHash},
                        utils::RawBuffer{ (const uint8_t*) &m_replicatorsUploadBytes[0],
                                            m_replicatorsUploadBytes.size() * sizeof (m_replicatorsUploadBytes[0]) },
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
                                utils::RawBuffer{ (const uint8_t*) &m_replicatorsUploadBytes[0],
                                m_replicatorsUploadBytes.size() * sizeof (m_replicatorsUploadBytes[0]) },
                                utils::RawBuffer{(const uint8_t*)&m_clientUploadBytes,sizeof(m_clientUploadBytes)}
                              },
                              m_signature );
    }
};

struct ApprovalTransactionInfo
{
    // Drive public key
    const std::string&  m_driveKey;

    // A reference to the transaction that initiated the modification
    const Hash256&      m_modifyTransactionHash;

    // Content Download Information for the File Structure
    const InfoHash&     m_rootHash;
    
    // The size of the “File Structure” File
    uint64_t            m_fsTreeFileSize;

    // The size of metafiles (torrents?,folders?) including “File Structure” File
    uint64_t            m_metaFilesSize;

    // Total used disk space. Must not be more than the Drive Size.
    uint64_t            m_driveSize;

    // Opinions about how much the Replicators and the Drive Owner have uploaded to this Replicator.
    std::vector<SingleApprovalTransactionInfo>   m_opinions;
};

using ModifyHandler = std::function<void( modify_status::code, const std::optional<ApprovalTransactionInfo>& info, const std::string& error )>;

//
// Replicator
//
class Replicator
{
public:

    virtual ~Replicator() = default;

    virtual void start() = 0;

    // All of the below functions return error string (or empty string)
    
    virtual std::string addDrive( const Key& driveKey, size_t driveSize ) = 0;

    virtual std::string removeDrive( const Key& driveKey ) = 0;

    virtual std::string modify( const Key&          driveKey,
                                ModifyRequest&&     modifyRequest,
                                ModifyHandler&&     handler ) = 0;

    virtual std::string cancelModify( const Key&        driveKey,
                                      const Hash256&    transactionHash ) = 0;

    // It will 'move' files from sandbox to drive
    virtual std::string acceptModifyApprovalTranaction( const Key&        driveKey,
                                                        const Hash256&    transactionHash ) = 0;

    virtual Hash256     getRootHash( const Key& driveKey ) = 0;

    virtual std::string loadTorrent( const Key& driveKey, const InfoHash& infoHash ) = 0;

    // 'replicatorsList' is used to notify other replictors
    // (it does not contain its own endpoint)
    virtual void        addDownloadChannelInfo( const std::array<uint8_t,32>&   channelKey,
                                                size_t                          prepaidDownloadSize,
                                                const ReplicatorList&           replicatorsList,
                                                std::vector<Key>&&        		clients ) = 0;

    virtual void        removeDownloadChannelInfo( const std::array<uint8_t,32>& channelId ) = 0;

    // 'replicatorsList' is used to verify other replictors receipts
    // (it does not contain its own endpoint)
//    virtual void        addModifyTransactionInfo( const std::array<uint8_t,32>& hash,
//                                                  const Key&                    clientPublicKey,
//                                                  size_t                        prepaidDownloadSize,
//                                                  const ReplicatorList&         replicatorsList,
//                                                  std::vector<const Key>&&      clients ) = 0;

    virtual uint64_t    receiptLimit() const = 0;

    virtual void        setReceiptLimit( uint64_t newLimitInBytes ) = 0;
    
    virtual void        printDriveStatus( const Key& driveKey ) = 0;
    
    virtual void        printTrafficDistribution( const std::array<uint8_t,32>&  transactionHash ) = 0;
    
    virtual const char* dbgReplicatorName() const = 0;

};

PLUGIN_API std::shared_ptr<Replicator> createDefaultReplicator(
                                               crypto::KeyPair&&,
                                               std::string&&  address,
                                               std::string&&  port,
                                               std::string&&  storageDirectory,
                                               std::string&&  sandboxDirectory,
                                               bool           useTcpSocket, // use TCP socket (instead of uTP)
                                               const char*    dbgReplicatorName = ""
);

}

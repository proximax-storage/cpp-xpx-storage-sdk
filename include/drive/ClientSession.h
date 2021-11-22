/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "drive/Session.h"
#include "drive/log.h"
#include <crypto/Signer.h>
#include <sirius_drive/session_delegate.h>

namespace sirius::drive {

class ClientSession : public lt::session_delegate, std::enable_shared_from_this<ClientSession>
{
    using DownloadChannelId     = std::optional<std::array<uint8_t,32>>;
    using ModifyTransactionHash = std::optional<std::array<uint8_t,32>>;
    using ReplicatorTraficMap   = std::map<std::array<uint8_t,32>,uint64_t>;

public:
    ClientSession( crypto::KeyPair&& keyPair, const char* dbgOurPeerName );

public:

    void setDownloadChannel( const ReplicatorList& replicatorList, Hash256 downloadChannelId, std::vector<uint64_t> alreadyReceivedSize = {} );

    void onHandshake( uint64_t /*uploadedSize*/ ) override;

    // Initiate file downloading (identified by downloadParameters.m_infoHash)
    void download( DownloadContext&& downloadParameters, const std::string&  tmpFolder );

    // prepare session to modify action
    InfoHash addActionListToSession( const ActionList&  actionList,
                                     const ReplicatorList& replicatorList,
                                     const sirius::Hash256& transactionHash,
                                     const std::string& workFolder );

    const std::optional<std::array<uint8_t,32>> downloadChannelId();

    void setSessionSettings(const lt::settings_pack& settings, bool localNodes)
    {
        m_session->lt_session().apply_settings(settings);
        if (localNodes) {
            std::uint32_t const mask = 1 << lt::session::global_peer_class_id;
            lt::ip_filter f;
            f.add_rule(lt::make_address("0.0.0.0"), lt::make_address("255.255.255.255"), mask);
            m_session->lt_session().set_peer_class_filter(f);
        }
    }

    // The next functions are called in libtorrent
protected:

    bool isClient() const override;

    bool acceptConnection( const std::array<uint8_t,32>&  transactionHash,
                           const std::array<uint8_t,32>&  peerPublicKey ) override;

    void onDisconnected( const std::array<uint8_t,32>&  transactionHash,
                         const std::array<uint8_t,32>&  peerPublicKey,
                         int                            reason ) override;

    bool checkDownloadLimit( const std::array<uint8_t,64>& /*reciept*/,
                             const std::array<uint8_t,32>& /*downloadChannelId*/,
                             uint64_t                      /*downloadedSize*/ ) override;

    virtual void signReceipt( const std::array<uint8_t,32>& downloadChannelId,
                              const std::array<uint8_t,32>& replicatorPublicKey,
                              uint64_t                      downloadedSize,
                              std::array<uint8_t,64>&       outSignature ) override;

    void signHandshake( const uint8_t* bytes, size_t size, std::array<uint8_t,64>& signature ) override;

    virtual bool verifyHandshake( const uint8_t* bytes, size_t size,
                                  const std::array<uint8_t,32>& publicKey,
                                  const std::array<uint8_t,64>& signature ) override;

    const std::array<uint8_t,32>& publicKey() override;

    void onPieceRequest(   const std::array<uint8_t,32>&  transactionHash,
                           const std::array<uint8_t,32>&  senderPublicKey,
                           uint64_t                       pieceSize ) override;

    void onPieceRequestReceived( const std::array<uint8_t,32>&  transactionHash,
                                 const std::array<uint8_t,32>&  receiverPublicKey,
                                 uint64_t                       pieceSize ) override;


    void onPieceSent( const std::array<uint8_t,32>&  transactionHash,
                      const std::array<uint8_t,32>&  receiverPublicKey,
                      uint64_t                       pieceSize ) override;

    void onPieceReceived( const std::array<uint8_t,32>&  /*transactionHash*/,
                          const std::array<uint8_t,32>&  senderPublicKey,
                          uint64_t                       pieceSize ) override;

    uint64_t requestedSize( const std::array<uint8_t,32>&  peerPublicKey ) override;

    uint64_t receivedSize( const std::array<uint8_t,32>&  peerPublicKey ) override;

    virtual void onMessageReceived( const std::string& query, const std::string& message ) override;

    const char* dbgOurPeerName() override;

private:
    friend std::shared_ptr<ClientSession> createClientSession( crypto::KeyPair&&,
                                                               const std::string&,
                                                               const LibTorrentErrorHandler&,
                                                               bool,
                                                               const char* );

    auto session();

private:
    std::shared_ptr<Session>    m_session;
    crypto::KeyPair             m_keyPair;

    DownloadChannelId           m_downloadChannelId;
    ReplicatorList              m_downloadReplicatorList;
    ReplicatorTraficMap         m_requestedSize;
    ReplicatorTraficMap         m_receivedSize;

    ModifyTransactionHash       m_modifyTransactionHash;
    ReplicatorList              m_modifyReplicatorList;

    const char*                 m_dbgOurPeerName;
};

// ClientSession creator
std::shared_ptr<ClientSession> createClientSession(  crypto::KeyPair&&             keyPair,
                                                            const std::string&            address,
                                                            const LibTorrentErrorHandler& errorHandler,
                                                            bool                          useTcpSocket, // instead of uTP
                                                            const char*                   dbgClientName = "" );
}

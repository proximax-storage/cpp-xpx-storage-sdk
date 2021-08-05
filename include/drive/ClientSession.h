/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "drive/Session.h"
#include "drive/log.h"
#include "crypto/Signer.h"
#include <sirius_drive/session_delegate.h>

namespace sirius { namespace drive {

class ClientSession : public lt::session_delegate, std::enable_shared_from_this<ClientSession>
{
    using DownloadChannelId = std::optional<std::array<uint8_t,32>>;

    std::shared_ptr<Session>    m_session;
    crypto::KeyPair             m_keyPair;

    DownloadChannelId           m_downloadChannelId;
    uint64_t                    m_downloadedSize = 0;
    uint64_t                    m_requestedSize = 0;

    const char*                 m_dbgOurPeerName;

public:
    ClientSession( crypto::KeyPair&& keyPair, const char* dbgOurPeerName )
    :
        m_keyPair( std::move(keyPair) ),
        m_dbgOurPeerName(dbgOurPeerName)
    {}

public:

    //
    void setDownloadChannel( Key downloadChannelId, uint64_t downloadedSize = 0 )
    {
        m_downloadChannelId = downloadChannelId.array();
        m_downloadedSize = downloadedSize;
    }

    void onHandshake( uint64_t /*uploadedSize*/ ) override
    {
        //todo here could be call back-call of test UI app
    }

    // Initiate file downloading (identified by downloadParameters.m_infoHash)
    void download( DownloadContext&&   downloadParameters,
                   const std::string&  tmpFolder,
                   endpoint_list       list )
    {
        if ( m_downloadChannelId )
        {
            m_session->download( std::move(downloadParameters), tmpFolder, list );
            return;
        }
        throw std::runtime_error("downloadChannel is not set");
    }

    // prepare session to modify action
    InfoHash addActionListToSession( const ActionList&  actionList,
                                     const std::string& workFolder,
                                     endpoint_list      list )
    {
        return m_session->addActionListToSession( actionList, workFolder, list );
    }

    // The next functions are called in libtorrent
protected:

    bool isClient() const override { return true; }

    bool checkDownloadLimit( const std::array<uint8_t,64>& /*reciept*/,
                             const std::array<uint8_t,32>& /*downloadChannelId*/,
                             uint64_t                      /*downloadedSize*/ ) override
    {
        // client does not check download limit
        return true;
    }

    void onPieceReceived( uint64_t pieceSize ) override
    {
        m_downloadedSize += pieceSize;
        //LOG( "++++++++++++ onPieceReceived '" << m_dbgOurPeerName << "' :" << m_downloadedSize << "     :" << pieceSize );
    }

    virtual void sign( const std::array<uint8_t,32>& replicatorPublicKey,
                       uint64_t&                     outDownloadedSize,
                       std::array<uint8_t,64>&       outSignature ) override
    {
        if ( m_downloadChannelId )
        {
            // sign the followingdata data:
            //  - replicator public key,
            //  - download channel hash,
            //  - downloaded size

            crypto::Sign( m_keyPair,
                         {  utils::RawBuffer{replicatorPublicKey},
                            utils::RawBuffer{*m_downloadChannelId},
                            utils::RawBuffer{(const uint8_t*)&m_downloadedSize,8} },
                         reinterpret_cast<Signature&>(outSignature) );

            outDownloadedSize = m_downloadedSize;
        }
    }

    bool verify( const std::array<uint8_t,32>&,
                 uint64_t,
                 const std::array<uint8_t,64>& ) override
    {
        // nothig to do
        return true;
    }

    void sign( const uint8_t* bytes, size_t size, std::array<uint8_t,64>& signature ) override
    {
        crypto::Sign( m_keyPair, utils::RawBuffer{bytes,size}, reinterpret_cast<Signature&>(signature) );
    }

    virtual bool verify( const uint8_t* bytes, size_t size,
                         const std::array<uint8_t,32>& publicKey,
                         const std::array<uint8_t,64>& signature ) override
    {
        return crypto::Verify( publicKey, utils::RawBuffer{bytes,size}, signature );
    }

    const std::array<uint8_t,32>& publicKey() override
    {
        return m_keyPair.publicKey().array();
    }

    const std::optional<std::array<uint8_t,32>> downloadChannelId() override
    {
        return m_downloadChannelId;
    }

    uint64_t downloadedSize( const std::array<uint8_t,32>& ) override
    {
        // for protocol compatibility we always retun 0; it could be ignored
        return 0;
    }

    void setDownloadedSize( uint64_t downloadedSize ) override
    {
        // 'downloadedSize' should be set to proper value (last 'downloadedSize' of peviuos peer_connection)
        m_downloadedSize = downloadedSize;
    }

    uint64_t downloadedSize() override
    {
        return m_downloadedSize;
    }

    uint64_t requestedSize() override
    {
        return m_requestedSize;
    }

    const char* dbgOurPeerName() override
    {
        return m_dbgOurPeerName;
    }

private:
    friend std::shared_ptr<ClientSession> createClientSession( crypto::KeyPair&&,
                                                               const std::string&,
                                                               const LibTorrentErrorHandler&,
                                                               bool,
                                                               const char* );

    auto session() { return m_session; }
};

// ClientSession creator
inline std::shared_ptr<ClientSession> createClientSession(  crypto::KeyPair&&             keyPair,
                                                            const std::string&            address,
                                                            const LibTorrentErrorHandler& errorHandler,
                                                            bool                          useTcpSocket, // instead of uTP
                                                            const char*                   dbgClientName = "" )
{
    std::shared_ptr<ClientSession> clientSession = std::make_shared<ClientSession>( std::move(keyPair), dbgClientName );
    clientSession->m_session = createDefaultSession( address, errorHandler, clientSession, useTcpSocket );
    clientSession->session()->lt_session().m_dbgOurPeerName = dbgClientName;
    return clientSession;
}

}}

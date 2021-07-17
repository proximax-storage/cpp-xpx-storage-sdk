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
    std::shared_ptr<Session>    m_session;
    crypto::KeyPair             m_keyPair;
    const char*                 m_dbgOurPeerName;

    Key     m_downloadChannelId;
    size_t  m_downloadedSize = 0;

public:
    ClientSession( crypto::KeyPair&& keyPair, const char* dbgOurPeerName )
    :
        m_keyPair( std::move(keyPair) ),
        m_dbgOurPeerName(dbgOurPeerName)
    {}

public:

    void setupDownloadChannel( Key downloadChannelId )
    {
        m_downloadChannelId = downloadChannelId;
        m_downloadedSize = 0;
    }

    // Initiate file downloading (identified by downloadParameters.m_infoHash)
    void download( DownloadContext&&   downloadParameters,
                    const std::string&  tmpFolder,
                    endpoint_list       list )
    {
        m_session->download( std::move(downloadParameters), tmpFolder, list );
    }

    // prepare session to modify action
    InfoHash addActionListToSession( const ActionList&  actionList,
                                     const std::string& workFolder,
                                     endpoint_list      list )
    {
        return m_session->addActionListToSession( actionList, workFolder, list );
    }

protected:

    bool checkDownloadLimit( std::vector<uint8_t> /*reciept*/,
                             lt::sha256_hash /*downloadChannelId*/,
                             size_t      /*downloadedSize*/ ) override
    {
        return true;
    }

    void onPiece( size_t pieceSize ) override
    {
        m_downloadedSize += pieceSize;
        LOG( "++++++++++++ " << m_downloadedSize << "     :" << pieceSize );
    }

    virtual void sign( const uint8_t* bytes, size_t size, std::array<uint8_t,64>& signature ) override
    {
        crypto::Sign( m_keyPair, utils::RawBuffer{bytes,size}, reinterpret_cast<Signature&>(signature) );
    }

    virtual bool verify( const std::array<uint8_t,32>& publicKey,
                        const uint8_t* bytes, size_t size,
                         const std::array<uint8_t,64>& signature ) override
    {
        return crypto::Verify( publicKey, utils::RawBuffer{bytes,size}, signature );
    }

    const std::array<uint8_t,32>& publicKey() override
    {
        return m_keyPair.publicKey().array();
    }

    virtual const char* dbgOurPeerName() override
    {
        return m_dbgOurPeerName;
    }

private:
    friend std::shared_ptr<ClientSession> createClientSession( crypto::KeyPair&&,
                                                               const std::string&,
                                                               const LibTorrentErrorHandler&,
                                                               const char* );

    auto session() { return m_session; }
};

inline std::shared_ptr<ClientSession> createClientSession(  crypto::KeyPair&&             keyPair,
                                                            const std::string&            address,
                                                            const LibTorrentErrorHandler& errorHandler,
                                                            const char*                   dbgClientName = "" )
{
    std::shared_ptr<ClientSession> clientSession = std::make_shared<ClientSession>( std::move(keyPair), dbgClientName );
    clientSession->session() = createDefaultSession( address, errorHandler, clientSession );
    clientSession->session()->lt_session().m_dbgOurPeerName = dbgClientName;
    return clientSession;
}

}}

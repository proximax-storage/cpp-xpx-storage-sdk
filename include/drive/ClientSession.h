/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "drive/Session.h"
#include "drive/log.h"
#include "drive/Utils.h"
#include "crypto/Signer.h"
#include <sirius_drive/session_delegate.h>

namespace sirius::drive {

class ClientSession : public lt::session_delegate, std::enable_shared_from_this<ClientSession>
{
    using DownloadChannelId     = std::optional<std::array<uint8_t,32>>;
    using ModifyTransactionHash = std::optional<std::array<uint8_t,32>>;
    using ReplicatorTraficMap   = std::map<std::array<uint8_t,32>,uint64_t>;

    std::shared_ptr<Session>    m_session;
    crypto::KeyPair             m_keyPair;

    DownloadChannelId           m_downloadChannelId;
    ReplicatorList              m_downloadReplicatorList;
    ReplicatorTraficMap         m_requestedSize;
    ReplicatorTraficMap         m_receivedSize;

//    ModifyTransactionHash       m_modifyTransactionHash;
//    ReplicatorList              m_modifyReplicatorList;

    const char*                 m_dbgOurPeerName;

public:
    ClientSession( crypto::KeyPair&& keyPair, const char* dbgOurPeerName )
    :
        m_keyPair( std::move(keyPair) ),
        m_dbgOurPeerName(dbgOurPeerName)
    {}

public:

    //
    void setDownloadChannel( const ReplicatorList& replicatorList, Hash256 downloadChannelId, std::vector<uint64_t> alreadyReceivedSize = {} )
    {
        m_downloadReplicatorList = replicatorList;
        m_downloadChannelId      = downloadChannelId.array();
        
        if ( replicatorList.size() == alreadyReceivedSize.size() )
        {
            for( uint32_t i=0; i<replicatorList.size(); i++ )
            {
                m_receivedSize[replicatorList[i].m_publicKey.array()]  = alreadyReceivedSize[i];
                m_requestedSize[replicatorList[i].m_publicKey.array()] = alreadyReceivedSize[i];
            }
        }
    }

    void onHandshake( uint64_t /*uploadedSize*/ ) override
    {
        //todo here could be call back-call of test UI app
    }

    // Initiate file downloading (identified by downloadParameters.m_infoHash)
    void download( DownloadContext&&   downloadParameters,
                   const std::string&  tmpFolder )
    {
        // check that download channel was set
        if ( !m_downloadChannelId )
            throw std::runtime_error("downloadChannel is not set");
        
        downloadParameters.m_transactionHash = *m_downloadChannelId;

        // check that replicator list is not empty
        if ( m_downloadReplicatorList.empty() )
            throw std::runtime_error("downloadChannel is not set");

        // start downloading
        m_session->download( std::move(downloadParameters), tmpFolder, m_downloadReplicatorList );
    }

    // prepare session to modify action
    InfoHash addActionListToSession( const ActionList&  actionList,
                                     const ReplicatorList& replicatorList,
                                     const std::string& workFolder )
    {
//        m_modifyReplicatorList = replicatorList;
//        m_modifyTransactionHash = transactionHash.array();
        
        // check that replicator list is not empty
//        if ( replicatorList.empty() )
//            throw std::runtime_error("modifyReplicatorList is empty");

        // create endpoint list for libtorrent
        endpoint_list endpointList;
        for( const auto& it : replicatorList )
            endpointList.emplace_back( it.m_endpoint );

        auto modificationWorkFolder = workFolder + "/" + drive::toString(drive::randomByteArray<Hash256>());

        auto hash = m_session->addActionListToSession( actionList, modificationWorkFolder, endpointList );
        return hash;
    }

    const std::optional<std::array<uint8_t,32>> downloadChannelId()
    {
        return m_downloadChannelId;
    }

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

    bool isClient() const override { return true; }
    
    bool acceptConnection( const std::array<uint8_t,32>&  /*transactionHash*/,
                           const std::array<uint8_t,32>&  /*peerPublicKey*/,
                           bool*                          /*outIsDownloadUnlimited*/ ) override
    {
        return true;
    }

    void onDisconnected( const std::array<uint8_t,32>&  transactionHash,
                         const std::array<uint8_t,32>&  peerPublicKey,
                         int                            reason ) override
    {
//        _LOG( "onDisconnected: " << dbgOurPeerName() << " from replicator: " << (int)peerPublicKey[0] );
//        _LOG( " - requestedSize: " << m_requestedSize[peerPublicKey] );
//        _LOG( " - receivedSize:  " << m_receivedSize[peerPublicKey] );
    }

    bool checkDownloadLimit( const std::array<uint8_t,64>& /*reciept*/,
                             const std::array<uint8_t,32>& /*downloadChannelId*/,
                             uint64_t                      /*downloadedSize*/ ) override
    {
        // client does not check download limit
        return true;
    }

    virtual void signReceipt( const std::array<uint8_t,32>& downloadChannelId,
                              const std::array<uint8_t,32>& replicatorPublicKey,
                              uint64_t                      downloadedSize,
                              std::array<uint8_t,64>&       outSignature ) override
    {
        assert( m_downloadChannelId );
        {
//todo++
            LOG( "SSS " << dbgOurPeerName() << " " << int(downloadChannelId[0]) << " " << (int)publicKey()[0] << " " << (int) replicatorPublicKey[0] << " " << downloadedSize );
            crypto::Sign( m_keyPair,
                          {
                            utils::RawBuffer{downloadChannelId},
                            utils::RawBuffer{publicKey()},
                            utils::RawBuffer{replicatorPublicKey},
                            utils::RawBuffer{(const uint8_t*)&downloadedSize,8}
                          },
                          reinterpret_cast<Signature&>(outSignature) );

//todo++
            LOG( "SSS: " << int(outSignature[0]) );
        }

        //todo++
//        if ( !verifyReceipt( downloadChannelId,
//                            publicKey(),       // client public key
//                            replicatorPublicKey,   // replicator public key
//                            downloadedSize, outSignature ) )
//        {
//            assert(0);
//        }
    }

    void signHandshake( const uint8_t* bytes, size_t size, std::array<uint8_t,64>& signature ) override
    {
        std::cout << "IN HANDSHAKE " << m_keyPair.publicKey() << std::endl;
        crypto::Sign( m_keyPair, utils::RawBuffer{bytes,size}, reinterpret_cast<Signature&>(signature) );
    }

    virtual bool verifyHandshake( const uint8_t* bytes, size_t size,
                                  const std::array<uint8_t,32>& publicKey,
                                  const std::array<uint8_t,64>& signature ) override
    {
        return crypto::Verify( publicKey, utils::RawBuffer{bytes,size}, signature );
    }

    const std::array<uint8_t,32>& publicKey() override
    {
        return m_keyPair.publicKey().array();
    }

//    void setStartReceivedSize( uint64_t downloadedSize ) override
//    {
//        // 'downloadedSize' should be set to proper value (last 'downloadedSize' of peviuos peer_connection)
//        m_receivedSize = downloadedSize;
//    }

    void onPieceRequest( const std::array<uint8_t,32>&  transactionHash,
                           const std::array<uint8_t,32>&  senderPublicKey,
                           uint64_t                       pieceSize ) override
    {
        m_requestedSize[senderPublicKey] += pieceSize;
    }
    
    void onPieceRequestReceived( const std::array<uint8_t,32>&  transactionHash,
                                 const std::array<uint8_t,32>&  receiverPublicKey,
                                 uint64_t                       pieceSize ) override
    {
    }

    
    void onPieceSent( const std::array<uint8_t,32>&  transactionHash,
                      const std::array<uint8_t,32>&  receiverPublicKey,
                      uint64_t                       pieceSize ) override
    {
        //todo++
    }

    void onPieceReceived( const std::array<uint8_t,32>&  /*transactionHash*/,
                          const std::array<uint8_t,32>&  senderPublicKey,
                          uint64_t                       pieceSize ) override
    {
        m_receivedSize[senderPublicKey] += pieceSize;
    }

    uint64_t requestedSize( const std::array<uint8_t,32>&  peerPublicKey ) override
    {
        return m_requestedSize[peerPublicKey];
    }

    uint64_t receivedSize( const std::array<uint8_t,32>&  peerPublicKey ) override
    {
        return m_receivedSize[peerPublicKey];
    }
    
    virtual void onMessageReceived( const std::string& query, const std::string& message ) override
    {
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
    //LOG( "creating: " << dbgClientName << " with key: " <<  int(keyPair.publicKey().array()[0]) )

    std::shared_ptr<ClientSession> clientSession = std::make_shared<ClientSession>( std::move(keyPair), dbgClientName );
    clientSession->m_session = createDefaultSession( address, errorHandler, clientSession, useTcpSocket );
    clientSession->session()->lt_session().m_dbgOurPeerName = dbgClientName;
    return clientSession;
}

}

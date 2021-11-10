/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "drive/ClientSession.h"

namespace sirius::drive {
    ClientSession::ClientSession( crypto::KeyPair&& keyPair, const char* dbgOurPeerName )
            :
            m_keyPair( std::move(keyPair) ),
            m_dbgOurPeerName(dbgOurPeerName)
    {}

    void ClientSession::setDownloadChannel( const ReplicatorList& replicatorList, Hash256 downloadChannelId, std::vector<uint64_t> alreadyReceivedSize )
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

    void ClientSession::onHandshake( uint64_t /*uploadedSize*/ )
    {
        //todo here could be call back-call of test UI app
    }

    // Initiate file downloading (identified by downloadParameters.m_infoHash)
    void ClientSession::download( DownloadContext&&   downloadParameters,
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
    InfoHash ClientSession::addActionListToSession( const ActionList&  actionList,
                                                    const ReplicatorList& replicatorList,
                                                    const sirius::Hash256& transactionHash,
                                                    const std::string& workFolder )
    {
        m_modifyReplicatorList = replicatorList;
        m_modifyTransactionHash = transactionHash.array();

        // check that replicator list is not empty
        if ( m_modifyReplicatorList.empty() )
            throw std::runtime_error("modifyReplicatorList is empty");

        // create endpoint list for libtorrent
        endpoint_list endpointList;
        for( const auto& it : replicatorList )
            endpointList.emplace_back( it.m_endpoint );

        return m_session->addActionListToSession( actionList, workFolder, endpointList );
    }

    const std::optional<std::array<uint8_t,32>> ClientSession::downloadChannelId()
    {
        return m_downloadChannelId;
    }

    bool ClientSession::isClient() const
    {
        return true;
    }

    bool ClientSession::acceptConnection( const std::array<uint8_t,32>&  transactionHash,
                                          const std::array<uint8_t,32>&  peerPublicKey )
    {
        return true;
    }

    void ClientSession::onDisconnected( const std::array<uint8_t,32>&  transactionHash,
                                        const std::array<uint8_t,32>&  peerPublicKey,
                                        int                            reason )
    {
//        _LOG( "onDisconnected: " << dbgOurPeerName() << " from replicator: " << (int)peerPublicKey[0] );
//        _LOG( " - requestedSize: " << m_requestedSize[peerPublicKey] );
//        _LOG( " - receivedSize:  " << m_receivedSize[peerPublicKey] );
    }

    bool ClientSession::checkDownloadLimit( const std::array<uint8_t,64>& /*reciept*/,
                                            const std::array<uint8_t,32>& /*downloadChannelId*/,
                                            uint64_t                      /*downloadedSize*/ )
    {
        // client does not check download limit
        return true;
    }

    void ClientSession::signReceipt( const std::array<uint8_t,32>& downloadChannelId,
                                     const std::array<uint8_t,32>& replicatorPublicKey,
                                     uint64_t                      downloadedSize,
                                     std::array<uint8_t,64>&       outSignature )
    {
        assert( m_downloadChannelId );
        {
//todo++
            LOG( "SSS " << dbgOurPeerName() << " " << int(downloadChannelId[0]) << " " << (int)publicKey()[0] << " " << (int) replicatorPublicKey[0] << " " << downloadedSize );
//            crypto::Sign( m_keyPair,
//                          {
//                            utils::RawBuffer{downloadChannelId},
//                            utils::RawBuffer{publicKey()},
//                            utils::RawBuffer{replicatorPublicKey},
//                            utils::RawBuffer{(const uint8_t*)&downloadedSize,8}
//                          },
//                          reinterpret_cast<Signature&>(outSignature) );

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

    void ClientSession::signHandshake( const uint8_t* bytes, size_t size, std::array<uint8_t,64>& signature )
    {
        //crypto::Sign( m_keyPair, utils::RawBuffer{bytes,size}, reinterpret_cast<Signature&>(signature) );
    }

    bool ClientSession::verifyHandshake( const uint8_t* bytes, size_t size,
                                         const std::array<uint8_t,32>& publicKey,
                                         const std::array<uint8_t,64>& signature )
    {
        //return crypto::Verify( publicKey, utils::RawBuffer{bytes,size}, signature );
        return true;
    }

    const std::array<uint8_t,32>& ClientSession::publicKey()
    {
        return m_keyPair.publicKey().array();
    }

    void ClientSession::onPieceRequest( const std::array<uint8_t,32>&  transactionHash,
                                        const std::array<uint8_t,32>&  senderPublicKey,
                                        uint64_t                       pieceSize )
    {
        m_requestedSize[senderPublicKey] += pieceSize;
    }

    void ClientSession::onPieceRequestReceived( const std::array<uint8_t,32>&  transactionHash,
                                                const std::array<uint8_t,32>&  receiverPublicKey,
                                                uint64_t                       pieceSize )
    {
    }


    void ClientSession::onPieceSent( const std::array<uint8_t,32>&  transactionHash,
                                     const std::array<uint8_t,32>&  receiverPublicKey,
                                     uint64_t                       pieceSize )
    {
        //todo++
    }

    void ClientSession::onPieceReceived( const std::array<uint8_t,32>&  /*transactionHash*/,
                                         const std::array<uint8_t,32>&  senderPublicKey,
                                         uint64_t                       pieceSize )
    {
        m_receivedSize[senderPublicKey] += pieceSize;
    }

    uint64_t ClientSession::requestedSize( const std::array<uint8_t,32>&  peerPublicKey )
    {
        return m_requestedSize[peerPublicKey];
    }

    uint64_t ClientSession::receivedSize( const std::array<uint8_t,32>&  peerPublicKey )
    {
        return m_receivedSize[peerPublicKey];
    }

    void ClientSession::onMessageReceived( const std::string& query, const std::string& message )
    {
    }

    const char* ClientSession::dbgOurPeerName()
    {
        return m_dbgOurPeerName;
    }

    auto ClientSession::session()
    {
        return m_session;
    }

// ClientSession creator
    std::shared_ptr<ClientSession> createClientSession(  crypto::KeyPair&&                    keyPair,
                                                         const std::string&            address,
                                                         const LibTorrentErrorHandler& errorHandler,
                                                         bool                          useTcpSocket, // instead of uTP
                                                         const char*                   dbgClientName )
    {
        _LOG( "creating: " << dbgClientName << " with key: " <<  int(keyPair.publicKey().array()[0]) )

        std::shared_ptr<ClientSession> clientSession = std::make_shared<ClientSession>( std::move(keyPair), dbgClientName );
        clientSession->m_session = createDefaultSession( address, errorHandler, clientSession, useTcpSocket );
        clientSession->session()->lt_session().m_dbgOurPeerName = dbgClientName;
        return clientSession;
    }

}

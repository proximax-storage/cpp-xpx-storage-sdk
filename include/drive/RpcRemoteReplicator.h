/*
*** Copyright 2022 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include "drive/RpcClient.h"
#include "drive/Utils.h"
#include "drive/Replicator.h"

namespace sirius::drive {

class RpcRemoteReplicator : public RpcClient, public ReplicatorEventHandler, public DbgReplicatorEventHandler
{
    std::shared_ptr<Replicator> m_replicator;
    crypto::KeyPair* m_keyPair = nullptr;
    
public:
    
    RpcRemoteReplicator() {}
    ~RpcRemoteReplicator() {
        if (m_replicator) {
            m_replicator->shutdownReplicator();
        }
        delete m_keyPair;
    }

    virtual void handleCommand( RPC_CMD command, cereal::PortableBinaryInputArchive* iarchivePtr ) override
    {
        cereal::PortableBinaryInputArchive& iarchive = *iarchivePtr;
        switch( command )
        {
            case RPC_CMD::createReplicator:
            {
                std::array<uint8_t,32> key;
                iarchive( key );

                std::string keyStr = toString( key );
                crypto::PrivateKey pKey = crypto::PrivateKey::FromStringSecure( (char*)keyStr.c_str(), keyStr.size() );
                m_keyPair = new crypto::KeyPair( crypto::KeyPair::FromPrivate( std::move(pKey) ) );
                
                std::string  address;
                iarchive( address );
                std::string  port;
                iarchive( port );
                std::string  wsPort;
                iarchive( wsPort );
                std::string  storageDirectory;
                iarchive( storageDirectory );
                std::string  logOptions;
                iarchive( logOptions );
                std::vector<ReplicatorInfo>  bootstraps;
                int bootstrapNumber;
                iarchive( bootstrapNumber );
                for( int i=0; i<bootstrapNumber; i++ )
                {
                    Key         pubKey;
                    std::string address;
                    uint16_t    port;
                    iarchive( pubKey );
                    iarchive( address );
                    iarchive( port );
                    bootstraps.emplace_back( ReplicatorInfo{{ boost::asio::ip::make_address(address), port }, pubKey} );
                }
                bool  dbgEventHandlerIsSet;
                iarchive( dbgEventHandlerIsSet );
                std::string dbgReplicatorName;
                iarchive( dbgReplicatorName );

                m_replicator = createDefaultReplicator(
                                     *m_keyPair,
                                     address,
                                     port,
                                     wsPort,
                                     storageDirectory,
                                     bootstraps,
                                     false, // use TCP socket (instead of uTP)
                                     *this,
                                     dbgEventHandlerIsSet ? this : nullptr,
                                     dbgReplicatorName,
                                     logOptions );
                
                RPC_LOG("Remote replicator created")
                break;
            }
            case RPC_CMD::destroyReplicator:
            {
                if (m_replicator) {
                    m_replicator->shutdownReplicator();
                    m_replicator.reset();
                }
                break;
            }
            case RPC_CMD::start:
            {
                //todo+++++
//                abort();
                m_replicator->start();
                break;
            }
            case RPC_CMD::asyncInitializationFinished:
            {
                m_replicator->asyncInitializationFinished();
                break;
            }
            case RPC_CMD::asyncModify:
            {
                Key                         driveKey;
                auto modifyRequest = std::make_unique<ModificationRequest>();
                iarchive( driveKey );
                iarchive( *modifyRequest );
                
                m_replicator->asyncModify( driveKey, std::move(modifyRequest) );
                break;
            }
            case RPC_CMD::asyncCancelModify:
            {
                Key       driveKey;
                Hash256   tx;
                iarchive( driveKey );
                iarchive( tx );
                
                m_replicator->asyncCancelModify( driveKey, tx );
                break;
            }
            case RPC_CMD::asyncAddDownloadChannelInfo:
            {
                Key                     driveKey;
                auto downloadRequest = std::make_unique<DownloadRequest>();
                bool                    mustBeSynchronized;
                iarchive( driveKey );
                iarchive( *downloadRequest );
                iarchive( mustBeSynchronized );

                m_replicator->asyncAddDownloadChannelInfo( driveKey, std::move(downloadRequest), mustBeSynchronized );
                break;
            }
            case RPC_CMD::asyncInitiateDownloadApprovalTransactionInfo:
            {
                Hash256 blockHash;
                Hash256 channelId;
                iarchive( blockHash );
                iarchive( channelId );

                m_replicator->asyncInitiateDownloadApprovalTransactionInfo( blockHash, channelId );
                break;
            }
            case RPC_CMD::asyncRemoveDownloadChannelInfo:
            {
                Hash256 channelId;
                iarchive( channelId );

                m_replicator->asyncRemoveDownloadChannelInfo( channelId );
                break;
            }
            case RPC_CMD::asyncIncreaseDownloadChannelSize:
            {
                Hash256 channelId;
                uint64_t size;
                iarchive( channelId );
                iarchive( size );

                m_replicator->asyncIncreaseDownloadChannelSize( channelId, size );
                break;
            }
            case RPC_CMD::asyncAddDrive:
            {
                Key                     driveKey;
                auto driveRequest = std::make_unique<AddDriveRequest>();
                iarchive( driveKey );
                iarchive( *driveRequest );

                m_replicator->asyncAddDrive( driveKey, std::move(driveRequest) );
                break;
            }
            case RPC_CMD::asyncRemoveDrive:
            {
                Key driveKey;
                iarchive( driveKey );

                m_replicator->asyncRemoveDrive( driveKey );
                break;
            }
            case RPC_CMD::asyncCloseDrive:
            {
                Key         driveKey;
                Hash256     transactionHash;
                iarchive( driveKey );
                iarchive( transactionHash );

                m_replicator->asyncCloseDrive( driveKey, transactionHash );
                break;
            }
            case RPC_CMD::asyncStartDriveVerification:
            {
                Key                         driveKey;
                auto request = std::make_unique<VerificationRequest>();
                iarchive( driveKey );
                iarchive( *request );

                m_replicator->asyncStartDriveVerification( driveKey, std::move(request) );
                break;
            }
            case RPC_CMD::asyncCancelDriveVerification:
            {
                Key                         driveKey;
                iarchive( driveKey );

                m_replicator->asyncCancelDriveVerification( driveKey );
                break;
            }
            case RPC_CMD::asyncSetReplicators:
            {
                Key                     driveKey;
                auto replicatorKeys = std::make_unique<std::vector<Key>>();
                iarchive( driveKey );
                iarchive( *replicatorKeys );

                m_replicator->asyncSetReplicators( driveKey, std::move(replicatorKeys) );
                break;
            }
            case RPC_CMD::asyncSetShardDonator:
            {
                Key                     driveKey;
                auto replicatorKeys = std::make_unique<std::vector<Key>>();
                iarchive( driveKey );
                iarchive( *replicatorKeys );

                m_replicator->asyncSetShardDonator( driveKey, std::move(replicatorKeys) );
                break;
            }
            case RPC_CMD::asyncSetShardRecipient:
            {
                Key                     driveKey;
                auto replicatorKeys = std::make_unique<std::vector<Key>>();
                iarchive( driveKey );
                iarchive( *replicatorKeys );

                m_replicator->asyncSetShardRecipient( driveKey, std::move(replicatorKeys) );
                break;
            }
            case RPC_CMD::asyncSetChanelShard:
            {
                auto channelId = std::make_unique<Hash256>();
                auto replicatorKeys = std::make_unique<std::vector<Key>>();
                iarchive( *channelId );
                iarchive( *replicatorKeys );

                m_replicator->asyncSetChanelShard( std::move(channelId), std::move(replicatorKeys) );
                break;
            }
            case RPC_CMD::asyncApprovalTransactionHasBeenPublished:
            {
                auto txInfo = std::make_unique<PublishedModificationApprovalTransactionInfo>();
                iarchive( *txInfo );

                m_replicator->asyncApprovalTransactionHasBeenPublished( std::move(txInfo) );
                break;
            }
            case RPC_CMD::asyncSingleApprovalTransactionHasBeenPublished:
            {
                auto txInfo = std::make_unique<PublishedModificationSingleApprovalTransactionInfo>();
                iarchive( *txInfo );

                m_replicator->asyncSingleApprovalTransactionHasBeenPublished( std::move(txInfo) );
                break;
            }
            case RPC_CMD::asyncDownloadApprovalTransactionHasBeenPublished:
            {
                Hash256 blockHash;
                Hash256 channelId;
                bool    driveIsClosed;
                iarchive( blockHash );
                iarchive( channelId );
                iarchive( driveIsClosed );

                m_replicator->asyncDownloadApprovalTransactionHasBeenPublished( blockHash, channelId, driveIsClosed );
                break;
            }
            case RPC_CMD::asyncVerifyApprovalTransactionHasBeenPublished:
            {
                PublishedVerificationApprovalTransactionInfo txInfo;
                iarchive( txInfo );

                m_replicator->asyncVerifyApprovalTransactionHasBeenPublished( txInfo );
                break;
            }
            case RPC_CMD::asyncOnOpinionReceived:
            {
                ApprovalTransactionInfo opinion;
                iarchive( opinion );

                m_replicator->asyncOnOpinionReceived( opinion );
                break;
            }
            case RPC_CMD::asyncOnDownloadOpinionReceived:
            {
                auto opinion = std::make_unique<DownloadApprovalTransactionInfo>();
                iarchive( *opinion );

                m_replicator->asyncOnDownloadOpinionReceived( std::move(opinion) );
                break;
            }
            case RPC_CMD::asyncApprovalTransactionHasFailedInvalidOpinions:
            {
                Key driveKey;
                Hash256 transactionHash;
                iarchive( driveKey );
                iarchive( transactionHash );

                m_replicator->asyncApprovalTransactionHasFailedInvalidOpinions( driveKey, transactionHash );
                break;
            }
            case RPC_CMD::asyncDownloadApprovalTransactionHasFailedInvalidOpinions:
            {
                Hash256 eventHash;
                Hash256 channelId;
                iarchive( eventHash );
                iarchive( channelId );

                m_replicator->asyncDownloadApprovalTransactionHasFailedInvalidOpinions( eventHash, channelId );
                break;
            }
            case RPC_CMD::asyncStartStream:
            {
                Key driveKey;
                auto request = std::make_unique<StreamRequest>();
                iarchive( driveKey );
                iarchive( *request );
                m_replicator->asyncStartStream( driveKey, std::move(request) );
                break;
            }
            case RPC_CMD::asyncIncreaseStream:
            {
                Key driveKey;
                auto request = std::make_unique<StreamIncreaseRequest>();
                iarchive( driveKey );
                iarchive( *request );
                m_replicator->asyncIncreaseStream( driveKey, std::move(request) );
                break;
            }
            case RPC_CMD::asyncFinishStreamTxPublished:
            {
                Key driveKey;
                auto request = std::make_unique<StreamFinishRequest>();
                iarchive( driveKey );
                iarchive( *request );
                m_replicator->asyncFinishStreamTxPublished( driveKey, std::move(request) );
                break;
            }
                
#ifndef SKIP_GRPC
			case RPC_CMD::setServiceAddress:
			{
				std::string address;
				iarchive( address );
				m_replicator->setServiceAddress( address );
				break;
			}
			case RPC_CMD::enableMessengerServer: {
				m_replicator->enableMessengerServer();
				break;
			}
			case RPC_CMD::enableSupercontractServer: {
				m_replicator->enableSupercontractServer();
				break;
			}
#endif

            case RPC_CMD::dbgGetRootHash:
            {
                DriveKey key;
                iarchive( key );

                auto hash = m_replicator->dbgGetRootHash( key );
                m_dnSocket.sendHashAnswer( RPC_CMD::dbgHash, hash.array() );

                break;
            }

            case RPC_CMD::dbgCrash:
            {
                int signalIndex;
                iarchive(signalIndex);

                RPC_LOG( "signalIndex: " << signalIndex )
                
                //
                // emulate signal for testing servive crash
                //
                switch( signalIndex )
                {
                    case 0:
                    {
                        raise(SIGILL);
                        break;
                    }
                    case 1:
                    {
                        int * p = (int*)0x0;
                        *p = 0;
                        break;
                    }
                    case 2:
                    {
//                        int * p = (int*)0x0;
//                        __LOG( "*p: " << *p )
                        break;
                    }
                    case 3:
                    {
                        char b[] = "i12453e";
                        lt::error_code ec;
                        lt::bdecode_node e = lt::bdecode(b, ec);
                        e.dict_find("unexisting");
                        break;
                    }
                    default:
                        break;
                }
                break;
            }
            case RPC_CMD::dbgLogMode: {
                uint8_t logMode;
                iarchive( logMode );

                RPC_LOG( "logMode: " << logMode )
                m_replicator->dbgSetLogMode( logMode );
                break;
            }

            default:
            {
                RPC_LOG( "Unexpected command received:" << static_cast<int>(command) );
                RPC_LOG( "Unexpected command received:" << CMD_STR(command) );
                rpcCall( RPC_CMD::onLibtorrentSessionError, "Unexpected command received:" + std::to_string(static_cast<int>(command)) );
                exit(1);
                break;
            }
        }
        
        sendAck();
    }

    virtual void handleError( std::error_code code ) override
    {
        __LOG( "ERROR occurred " << code.message() );
        exit(1);
    }

    virtual void handleConnectionLost() override
    {
        
    }

    virtual void verificationTransactionIsReady( Replicator&                    replicator,
                                                const VerifyApprovalTxInfo&     transactionInfo ) override
    {
        rpcCall( RPC_CMD::verificationTransactionIsReady, transactionInfo );
    }
    
    // It will initiate the approving of modify transaction
    virtual void modifyApprovalTransactionIsReady( Replicator& replicator, const ApprovalTransactionInfo& transactionInfo ) override
    {
        rpcCall( RPC_CMD::modifyApprovalTransactionIsReady, transactionInfo );
    }
    
    // It will initiate the approving of single modify transaction
    virtual void singleModifyApprovalTransactionIsReady( Replicator& replicator, const ApprovalTransactionInfo& transactionInfo ) override
    {
        rpcCall( RPC_CMD::singleModifyApprovalTransactionIsReady, transactionInfo );
    }
    
    // It will be called when transaction could not be completed
    virtual void downloadApprovalTransactionIsReady( Replicator& replicator, const DownloadApprovalTransactionInfo& transactionInfo ) override
    {
        rpcCall( RPC_CMD::downloadApprovalTransactionIsReady, transactionInfo );
    }
    
    virtual void opinionHasBeenReceived(  Replicator& replicator,
                                          const ApprovalTransactionInfo& transactionInfo ) override
    {
        rpcCall( RPC_CMD::opinionHasBeenReceived, transactionInfo );
    }
    
    virtual void downloadOpinionHasBeenReceived(  Replicator& replicator,
                                                  const DownloadApprovalTransactionInfo& transactionInfo ) override
    {
        rpcCall( RPC_CMD::downloadOpinionHasBeenReceived, transactionInfo );
    }
    
    virtual void onLibtorrentSessionError( const std::string& message ) override
    {
        rpcCall( RPC_CMD::onLibtorrentSessionError, message );
    }
    
    
    //////////////////////////////////

    virtual void driveModificationIsCompleted( Replicator&                    replicator,
                                               const sirius::Key&             driveKey,
                                               const Hash256&                 modifyTransactionHash,
                                               const sirius::drive::InfoHash& rootHash ) override
    {
        rpcCall( RPC_CMD::driveModificationIsCompleted, driveKey, modifyTransactionHash, rootHash );
    }

    virtual void rootHashIsCalculated( Replicator&                    replicator,
                                       const sirius::Key&             driveKey,
                                       const Hash256&                 modifyTransactionHash,
                                       const sirius::drive::InfoHash& sandboxRootHash ) override
    {
        rpcCall( RPC_CMD::rootHashIsCalculated, driveKey, modifyTransactionHash, sandboxRootHash );
    }
    
    virtual void willBeTerminated( Replicator& replicator ) override
    {
        rpcCall( RPC_CMD::willBeTerminated );
    }

    virtual void driveAdded( const sirius::Key& driveKey ) override
    {
        //rpcCall( RPC_CMD::driveAdded, driveKey );
    }

    virtual void driveIsInitialized( Replicator&                    replicator,
                                     const sirius::Key&             driveKey,
                                     const sirius::drive::InfoHash& rootHash ) override
    {
        //rpcCall( RPC_CMD::driveIsInitialized, driveKey, rootHash );
    }

    virtual void driveIsClosed(  Replicator&                replicator,
                                 const sirius::Key&         driveKey,
                                 const Hash256&             transactionHash ) override
    {
        //rpcCall( RPC_CMD::driveIsClosed, driveKey, transactionHash );
    }

    virtual void driveIsRemoved(  Replicator&                replicator,
                                  const sirius::Key&         driveKey ) override
    {
        //rpcCall( RPC_CMD::driveIsRemoved, driveKey );
    }

    virtual void  driveModificationIsCanceled(  Replicator&                  replicator,
                                               const sirius::Key&           driveKey,
                                               const Hash256&               modifyTransactionHash ) override
    {
        //rpcCall( RPC_CMD::driveModificationIsCanceled, driveKey, modifyTransactionHash );
    }

    virtual void modifyTransactionEndedWithError( Replicator&               replicator,
                                                 const sirius::Key&         driveKey,
                                                 const ModificationRequest& modifyRequest,
                                                 const std::string&         reason,
                                                 int                        errorCode ) override
    {
        rpcCall( RPC_CMD::modifyTransactionEndedWithError, driveKey, modifyRequest, reason, errorCode );
    }


};

}

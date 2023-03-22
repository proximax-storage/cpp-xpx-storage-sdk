/*
*** Copyright 2022 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include "drive/Replicator.h"
#include "drive/RpcServer.h"
#include <unistd.h>
#include <filesystem>

namespace fs = std::filesystem;

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <limits.h>
#endif

namespace fs = std::filesystem;

namespace sirius::drive {

//
// RpcReplicator
//
class RpcReplicator : public Replicator, public RpcServer
{
    Key     m_unusedKey;
    Hash256 m_unusedHash;
    
    const crypto::KeyPair&          m_keyPair;
    std::string                     m_address;
    std::string                     m_port;
    std::string                     m_storageDirectory;
    std::string                     m_sandboxDirectory;
    const std::vector<ReplicatorInfo> m_bootstraps;
    bool                            m_useTcpSocket;
    ReplicatorEventHandler&         m_eventHandler;
    DbgReplicatorEventHandler*      m_dbgEventHandler;
    const std::string               m_dbgReplicatorName;
    
    bool                            m_isRemoteServiceConnected = false;
    
    std::uint16_t                   m_rpcPort;
    
public:
    RpcReplicator(
              const crypto::KeyPair& keyPair,
              std::string&&  address,
              std::string&&  port,
              std::string&&  storageDirectory,
              std::string&&  sandboxDirectory,
              const std::vector<ReplicatorInfo>&  bootstraps,
              bool           useTcpSocket, // use TCP socket (instead of uTP)
              ReplicatorEventHandler&       eventHandler,
              DbgReplicatorEventHandler*    dbgEventHandler = nullptr,
              const std::string&            dbgReplicatorName = ""
            )
              : RpcServer(),
                m_keyPair(keyPair),
                m_address(address),
                m_port(port),
                m_storageDirectory(storageDirectory),
                m_sandboxDirectory(sandboxDirectory),
                m_bootstraps(bootstraps),
                m_useTcpSocket(useTcpSocket),
                m_eventHandler(eventHandler),
                m_dbgEventHandler(dbgEventHandler),
                m_dbgReplicatorName(dbgReplicatorName)
    {
    }
    
    void startTcpServer( std::string address, std::uint16_t port )
    {
        m_rpcPort = port;

        RpcTcpServer::startTcpServer( address, port );

#ifndef DEBUG_NO_DAEMON_REPLICATOR_SERVICE
        startRemoteReplicator();
#endif

        __LOG("Waiting Remote Replicator Service connection...")
        for( int i=0; i<6000; i++) // wait 60 secs
        {
            if ( m_isRemoteServiceConnected )
            {
                __LOG("...Remote Replicator Service connected")
                return;
            }
            usleep(10000);
        }
        
        _LOG_ERR("Remote Replicator Service not connected!")
    }

    ~RpcReplicator() override
    {
        rpcCall( RPC_CMD::destroyReplicator );
    }
    
    virtual bool isConnectionLost() const override
    {
        return m_isConnectionLost;
    };

    
    virtual void startRemoteReplicator()
    {
#ifdef __linux__
        char path[PATH_MAX+1] = { 0 };
    	int nchar = readlink("/proc/self/exe", path, sizeof(path) );

    	if ( nchar < 0 ) {
    		_LOG_ERR("Invalid Read Link")
			return;
		}

		path[nchar] = 0;

#elif __APPLE__

        char path[PATH_MAX] = { 0 };
        uint32_t bufsize = PATH_MAX;
        if( int rc = _NSGetExecutablePath( path, &bufsize); rc )
        {
            _LOG_ERR("Error: _NSGetExecutablePath: " << rc )
        }

#endif

        std::string executable = std::filesystem::path{path}.parent_path() / "replicator-service";
        auto cmd = (executable + " -d 127.0.0.1 " + std::to_string(m_rpcPort) );
        std::system( cmd.c_str() );
        __LOG( "std::system( cmd.c_str() );" );
    }
    
    virtual void initiateRemoteService() override
    {
        __LOG("Remote replicator created")
        
        m_isRemoteServiceConnected = true;
    }

    virtual void handleCommand( RPC_CMD command, cereal::PortableBinaryInputArchive* iarchive )  override
    {
        try
        {
            switch( command )
            {
                case RPC_CMD::verificationTransactionIsReady:
                {
                    VerifyApprovalTxInfo transactionInfo;
                    (*iarchive)( transactionInfo );
                    m_eventHandler.verificationTransactionIsReady( *this, transactionInfo );
                    break;
                }
                case RPC_CMD::modifyApprovalTransactionIsReady:
                {
                    ApprovalTransactionInfo transactionInfo;
                    (*iarchive)( transactionInfo );
                    m_eventHandler.modifyApprovalTransactionIsReady( *this, transactionInfo );
                    break;
                }
                case RPC_CMD::singleModifyApprovalTransactionIsReady:
                {
                    ApprovalTransactionInfo transactionInfo;
                    (*iarchive)( transactionInfo );
                    m_eventHandler.singleModifyApprovalTransactionIsReady( *this, transactionInfo );
                    break;
                }
                case RPC_CMD::downloadApprovalTransactionIsReady:
                {
                    DownloadApprovalTransactionInfo transactionInfo;
                    (*iarchive)( transactionInfo );
                    m_eventHandler.downloadApprovalTransactionIsReady( *this, transactionInfo );
                    break;
                }
                case RPC_CMD::opinionHasBeenReceived:
                {
                    ApprovalTransactionInfo transactionInfo;
                    (*iarchive)( transactionInfo );
                    m_eventHandler.opinionHasBeenReceived( *this, transactionInfo );
                    break;
                }
                case RPC_CMD::downloadOpinionHasBeenReceived:
                {
                    DownloadApprovalTransactionInfo transactionInfo;
                    (*iarchive)( transactionInfo );
                    m_eventHandler.downloadOpinionHasBeenReceived( *this, transactionInfo );
                    break;
                }
                case RPC_CMD::onLibtorrentSessionError:
                {
                    std::string message;
                    (*iarchive)( message );
                    m_eventHandler.onLibtorrentSessionError( message );
                    break;
                }
                    
                /// For debugging
                case RPC_CMD::driveModificationIsCompleted:
                {
                    sirius::Key             driveKey;
                    sirius::drive::InfoHash modifyTransactionHash;
                    sirius::drive::InfoHash sandboxRootHash;
                    (*iarchive)( driveKey );
                    (*iarchive)( modifyTransactionHash );
                    (*iarchive)( sandboxRootHash );
                    m_dbgEventHandler->driveModificationIsCompleted( *this, driveKey, modifyTransactionHash, sandboxRootHash );
                    break;
                }

                case RPC_CMD::rootHashIsCalculated:
                {
                    sirius::Key             driveKey;
                    sirius::drive::InfoHash modifyTransactionHash;
                    sirius::drive::InfoHash sandboxRootHash;
                    (*iarchive)( driveKey );
                    (*iarchive)( modifyTransactionHash );
                    (*iarchive)( sandboxRootHash );
                    m_dbgEventHandler->rootHashIsCalculated( *this, driveKey, modifyTransactionHash, sandboxRootHash );
                    break;
                }
                case RPC_CMD::modifyTransactionEndedWithError:
                {
                    sirius::Key             driveKey;
                    ModificationRequest     modifyRequest;
                    std::string             reason;
                    int                     errorCode;
                    (*iarchive)( driveKey );
                    (*iarchive)( modifyRequest );
                    (*iarchive)( reason );
                    (*iarchive)( errorCode );
                    m_dbgEventHandler->modifyTransactionEndedWithError( *this, driveKey, modifyRequest, reason, errorCode );
                    break;
                }

//                case RPC_CMD::driveAdded:
//                case RPC_CMD::driveIsInitialized:
//                case RPC_CMD::driveIsClosed:
//                case RPC_CMD::driveIsRemoved:
//                case RPC_CMD::driveModificationIsCanceled:

                default:
                    __LOG( "invalid comand received: " << CMD_STR(command) );
                    break;
            }
        }
        catch( std::runtime_error& e  )
        {
            __LOG( "RpcReplicator::handleCommand: command: " << CMD_STR(command) << "; exception: " << e.what() )
        }
    }

    virtual void handleError( std::error_code )  override
    {
        
    }

    virtual void start() override
    {
        std::ostringstream os( std::ios::binary );
        cereal::PortableBinaryOutputArchive archive( os );

        class MyKeyPair {
        public:
            Key m_privateKey;
            Key m_publicKey;
        };

        const Key& pKey = reinterpret_cast<const MyKeyPair&>(m_keyPair).m_privateKey;
        archive( pKey.array() );
        
        archive( m_address );
        archive( m_port );
        archive( m_storageDirectory );
        archive( m_sandboxDirectory );
        int bootstrapNumber = (int) m_bootstraps.size();
        archive( bootstrapNumber );
        for( int i=0; i<bootstrapNumber; i++ )
        {
            const Key&  pubKey  = m_bootstraps[i].m_publicKey;
            std::string address = m_bootstraps[i].m_endpoint.address().to_string();
            uint16_t    port    = m_bootstraps[i].m_endpoint.port();
            archive( pubKey );
            archive( address );
            archive( port );
        }
        bool  dbgEventHandlerIsSet = (m_dbgEventHandler != nullptr);
        archive( dbgEventHandlerIsSet );
        archive( m_dbgReplicatorName );

        __LOG( "os.str().size(): " << os.str().size() );
        __LOG( "os.str(): " << int(os.str()[0]) << " "<< int(os.str()[1]) << " "<< int(os.str()[2]) << " "<< int(os.str()[3]) << " " );
        rpcCallArchStr( RPC_CMD::createReplicator, os.str() );

        rpcCall( RPC_CMD::start );
    }

    virtual const Key& replicatorKey() const override { __ASSERT(0); return m_unusedKey; }

    virtual void asyncInitializationFinished() override
    {
        rpcCall( RPC_CMD::asyncInitializationFinished );
    }

    virtual void asyncAddDrive( Key driveKey, mobj<AddDriveRequest>&& driveRequest ) override
    {
        rpcCall( RPC_CMD::asyncAddDrive, driveKey, *driveRequest );
    }

    virtual void asyncRemoveDrive( Key driveKey ) override
    {
        rpcCall( RPC_CMD::asyncRemoveDrive, driveKey );
    }

	virtual void asyncSetReplicators( Key driveKey, mobj<std::vector<Key>>&& replicatorKeys ) override
    {
		rpcCall( RPC_CMD::asyncSetReplicators, driveKey, *replicatorKeys );
    }

    virtual void asyncSetShardDonator( Key driveKey, mobj<std::vector<Key>>&& replicatorKeys ) override
    {
		rpcCall( RPC_CMD::asyncSetShardDonator, driveKey, *replicatorKeys );
    }

    virtual void asyncSetShardRecipient( Key driveKey, mobj<std::vector<Key>>&& replicatorKeys ) override
    {
		rpcCall( RPC_CMD::asyncSetShardRecipient, driveKey, *replicatorKeys );
    }

    virtual void asyncSetChanelShard( mobj<Hash256>&& channelId, mobj<ReplicatorList>&& replicatorKeys ) override
    {
        rpcCall( RPC_CMD::asyncSetChanelShard, *channelId, *replicatorKeys );
    }

    virtual void asyncCloseDrive( Key driveKey, Hash256 transactionHash ) override
    {
        rpcCall( RPC_CMD::asyncCloseDrive, driveKey, transactionHash );
    }

    virtual void        asyncModify( Key driveKey, mobj<ModificationRequest>&&  modifyRequest ) override
    {
		rpcCall( RPC_CMD::asyncModify, driveKey, *modifyRequest );
    }

    virtual void        asyncCancelModify( Key driveKey, Hash256  transactionHash ) override
    {
		rpcCall( RPC_CMD::asyncCancelModify, driveKey, transactionHash );
    }
    
    virtual void        asyncStartDriveVerification( Key driveKey, mobj<VerificationRequest>&& request) override
    {
		rpcCall( RPC_CMD::asyncStartDriveVerification, driveKey, *request );
    }
    
    virtual void        asyncCancelDriveVerification( Key driveKey ) override
    {
		rpcCall( RPC_CMD::asyncCancelDriveVerification, driveKey );
    }

    virtual void        asyncStartStream( Key driveKey, mobj<StreamRequest>&& ) override {}
    virtual void        asyncIncreaseStream( Key driveKey, mobj<StreamIncreaseRequest>&& ) override {}
    virtual void        asyncFinishStreamTxPublished( Key driveKey, mobj<StreamFinishRequest>&& ) override {}

    virtual void        asyncAddDownloadChannelInfo( Key driveKey, mobj<DownloadRequest>&&  downloadRequest, bool mustBeSynchronized = false ) override
    {
		rpcCall( RPC_CMD::asyncAddDownloadChannelInfo, driveKey, *downloadRequest, mustBeSynchronized );
    }

	virtual void		asyncIncreaseDownloadChannelSize( ChannelId channelId, uint64_t size ) override
    {
		rpcCall( RPC_CMD::asyncIncreaseDownloadChannelSize, channelId, size );
    }

    virtual void        asyncRemoveDownloadChannelInfo( ChannelId channelId ) override
    {
		rpcCall( RPC_CMD::asyncRemoveDownloadChannelInfo, channelId );
    }

    virtual void        asyncOnDownloadOpinionReceived( mobj<DownloadApprovalTransactionInfo>&& anOpinion ) override {
        rpcCall( RPC_CMD::asyncOnDownloadOpinionReceived, *anOpinion );
    }
    
    virtual void        asyncInitiateDownloadApprovalTransactionInfo( Hash256 blockHash, Hash256 channelId ) override
    {
		rpcCall( RPC_CMD::asyncInitiateDownloadApprovalTransactionInfo, blockHash, channelId );
    }

    virtual void        asyncDownloadApprovalTransactionHasBeenPublished( Hash256 blockHash, Hash256 channelId, bool driveIsClosed = false ) override
    {
		rpcCall( RPC_CMD::asyncDownloadApprovalTransactionHasBeenPublished, blockHash, channelId, driveIsClosed );
    }

    virtual void        asyncDownloadApprovalTransactionHasFailedInvalidOpinions( Hash256 eventHash, Hash256 channelId ) override {
        rpcCall( RPC_CMD::asyncDownloadApprovalTransactionHasFailedInvalidOpinions, eventHash, channelId );
    }

    virtual void        asyncOnOpinionReceived( ApprovalTransactionInfo anOpinion ) override {
        rpcCall( RPC_CMD::asyncOnOpinionReceived, anOpinion );
    }
    
    virtual void        asyncApprovalTransactionHasBeenPublished( mobj<PublishedModificationApprovalTransactionInfo>&& transaction ) override
    {
		rpcCall( RPC_CMD::asyncApprovalTransactionHasBeenPublished, *transaction );
    }

    virtual void        asyncApprovalTransactionHasFailedInvalidOpinions( Key driveKey, Hash256 transactionHash ) override {
        rpcCall( RPC_CMD::asyncApprovalTransactionHasFailedInvalidOpinions, driveKey, transactionHash );
    }

    virtual void        asyncSingleApprovalTransactionHasBeenPublished( mobj<PublishedModificationSingleApprovalTransactionInfo>&& transaction ) override
    {
		rpcCall( RPC_CMD::asyncSingleApprovalTransactionHasBeenPublished, *transaction );
    }

    virtual void        asyncVerifyApprovalTransactionHasBeenPublished( PublishedVerificationApprovalTransactionInfo info ) override
    {
		rpcCall( RPC_CMD::asyncVerifyApprovalTransactionHasBeenPublished, info );
    }

    virtual void        asyncVerifyApprovalTransactionHasFailedInvalidOpinions( Key driveKey, Hash256 verificationId ) override {
        __ASSERT(0);
    }

    virtual uint64_t    receiptLimit() const override { __ASSERT(0); return 0; }
    virtual void        setReceiptLimit( uint64_t newLimitInBytes ) override {}

    virtual void        setDownloadApprovalTransactionTimerDelay( int milliseconds ) override {}
    virtual void        setModifyApprovalTransactionTimerDelay( int milliseconds ) override {}
    virtual int         getModifyApprovalTransactionTimerDelay() override { __ASSERT(0); return 0; }
    virtual void        setVerifyCodeTimerDelay( int milliseconds ) override {}
    virtual int         getVerifyCodeTimerDelay() override { __ASSERT(0); return 0; }
    virtual void        setVerifyApprovalTransactionTimerDelay( int milliseconds ) override {}
    virtual int         getVerifyApprovalTransactionTimerDelay() override { __ASSERT(0); return 0; }
    virtual void        setVerificationShareTimerDelay( int milliseconds ) override {}
    virtual int         getVerificationShareTimerDelay() override { __ASSERT(0); return 0; }
    virtual void        setMinReplicatorsNumber( uint64_t number ) override {}
    virtual uint64_t    getMinReplicatorsNumber() override { __ASSERT(0); return 0; }
    virtual void        setSessionSettings(const lt::settings_pack&, bool localNodes) override {}

    void initiateManualModifications( const DriveKey& driveKey, const InitiateModificationsRequest& request ) override
    {

    }

    void initiateManualSandboxModifications( const DriveKey& driveKey,
                                             const InitiateSandboxModificationsRequest& request ) override
    {

    }

    void openFile( const DriveKey& driveKey, const OpenFileRequest& request ) override
    {

    }

    void writeFile( const DriveKey& driveKey, const WriteFileRequest& request ) override
    {

    }

    void readFile( const DriveKey& driveKey, const ReadFileRequest& request ) override
    {

    }

    void flush( const DriveKey& driveKey, const FlushRequest& request ) override
    {

    }

    void closeFile( const DriveKey& driveKey, const CloseFileRequest& request ) override
    {

    }

    void removeFsTreeEntry( const DriveKey& driveKey, const RemoveFilesystemEntryRequest& request ) override
    {

    }

    void pathExist( const DriveKey& driveKey, const PathExistRequest& request ) override
    {

    }

    void pathIsFile( const DriveKey& driveKey, const PathIsFileRequest& request ) override
    {

    }

    void createDirectories( const DriveKey& driveKey, const CreateDirectoriesRequest& request ) override
    {

    }


    void folderIteratorCreate( const DriveKey& driveKey, const FolderIteratorCreateRequest& request ) override
    {

    }

    void folderIteratorDestroy( const DriveKey& driveKey, const FolderIteratorDestroyRequest& request ) override
    {

    }

    void folderIteratorHasNext( const DriveKey& driveKey, const FolderIteratorHasNextRequest& request ) override
    {

    }

    void folderIteratorNext( const DriveKey& driveKey, const FolderIteratorNextRequest& request ) override
    {

    }

    void moveFsTreeEntry( const DriveKey& driveKey, const MoveFilesystemEntryRequest& request ) override
    {

    }

    void applySandboxManualModifications( const DriveKey& driveKey,
                                          const ApplySandboxModificationsRequest& request ) override
    {

    }

    void evaluateStorageHash( const DriveKey& driveKey, const EvaluateStorageHashRequest& request ) override
    {

    }

    void applyStorageManualModifications( const DriveKey& driveKey,
                                          const ApplyStorageModificationsRequest& request ) override
    {

    }

    void manualSynchronize( const DriveKey& driveKey, const SynchronizationRequest& request ) override
    {

    }

    void getAbsolutePath( const DriveKey& driveKey, const AbsolutePathRequest& request ) override
    {

    }

    void getActualModificationId( const DriveKey& driveKey, const ActualModificationIdRequest& request ) override
    {

    }

    void getFilesystem( const DriveKey& driveKey, const FilesystemRequest& request ) override
    {

    }

    void setServiceAddress( const std::string& address ) override {
    	rpcCall( RPC_CMD::setServiceAddress, address );
    }

    void enableSupercontractServer() override {
    	rpcCall( RPC_CMD::enableSupercontractServer );
    }

    void enableMessengerServer() override {
    	rpcCall( RPC_CMD::enableMessengerServer );
    }

    virtual Hash256     dbgGetRootHash( const DriveKey& driveKey ) override
    {
        //__ASSERT(0); return m_unusedHash;
        auto hash = rpcDbgGetRootHash( driveKey );
        return hash;
    }
    
    virtual void        dbgPrintDriveStatus( const Key& driveKey ) override {}
    virtual void        dbgPrintTrafficDistribution( std::array<uint8_t,32>  transactionHash ) override {}
    virtual std::string dbgReplicatorName() const override { return m_dbgReplicatorName; }
    virtual const Key&  dbgReplicatorKey() const override { return m_keyPair.publicKey(); }

    void dbgSetLogMode( uint8_t mode ) override
    {
        rpcCall( RPC_CMD::dbgLogMode, mode );
    }

    virtual void dbgEmulateSignal( int index ) override
    {
        rpcCall( RPC_CMD::dbgCrash, index );
    }

};

PLUGIN_API std::shared_ptr<Replicator> createRpcReplicator(
                                               std::string&&            rpcAddress,
                                               int                      rpcPort,
                                               const crypto::KeyPair&   keyPair,
                                               std::string&&            address,
                                               std::string&&            port,
                                               std::string&&            storageDirectory,
                                               std::string&&            sandboxDirectory,
                                               const std::vector<ReplicatorInfo>&  bootstraps,
                                               bool                     useTcpSocket, // use TCP socket (instead of uTP)
                                               ReplicatorEventHandler&  handler,
                                               DbgReplicatorEventHandler*  dbgEventHandler = nullptr,
                                               const std::string&    dbgReplicatorName = "" )
{
    std::error_code ec;
    auto absoluteStorageDirectory = fs::absolute(storageDirectory, ec);
    if (ec)
    {
        _LOG_ERR( "Unable To Find Absolute Path Of " << storageDirectory << ": " << ec.message() );
    }

    auto absoluteSandboxDirectory = fs::absolute(sandboxDirectory, ec);
    if (ec)
    {
        _LOG_ERR( "Unable To Find Absolute Path Of " << sandboxDirectory << ": " << ec.message() );
    }

    auto replicator = std::make_shared<RpcReplicator>(
                        keyPair,
                        std::move( address ),
                        std::move(port),
                        std::move( absoluteStorageDirectory ),
                        std::move( absoluteSandboxDirectory ),
                        bootstraps,
                        useTcpSocket,
                        handler,
                        dbgEventHandler,
                        dbgReplicatorName );

    replicator->startTcpServer( rpcAddress, rpcPort );

    return replicator;
}

}

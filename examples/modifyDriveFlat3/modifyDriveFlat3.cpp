#include "types.h"
#include "drive/Session.h"
#include "drive/ClientSession.h"
#include "drive/Replicator.h"
#include "drive/FlatDrive.h"
#include "drive/FsTree.h"
#include "drive/Utils.h"
#include "crypto/Signer.h"

#include <fstream>
#include <filesystem>
#include <future>
#include <condition_variable>
#include "boost/date_time/posix_time/posix_time.hpp"

#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/kademlia/ed25519.hpp>

#include <sirius_drive/session_delegate.h>

const bool gUse3Replicators = true;

//
// This example shows interaction between 'client' and 'replicator'.
//

#define BIG_FILE_SIZE 10 * 1024*1024 //150//4
#define TRANSPORT_PROTOCOL false // true - TCP, false - uTP

// !!!
// CLIENT_IP_ADDR should be changed to proper address according to your network settings (see ifconfig)

#define CLIENT_IP_ADDR          "192.168.2.200"
#define CLIENT_PORT             5000

#define REPLICATOR_IP_ADDR      "192.168.2.101"
#define REPLICATOR_PORT         5001
#define REPLICATOR_IP_ADDR_2    "192.168.2.102"
#define REPLICATOR_PORT_2       5002
#define REPLICATOR_IP_ADDR_3    "192.168.2.103"
#define REPLICATOR_PORT_3       5003

#define ROOT_TEST_FOLDER                fs::path(getenv("HOME")) / "111"
#define REPLICATOR_ROOT_FOLDER          fs::path(getenv("HOME")) / "111" / "replicator_root"
#define REPLICATOR_SANDBOX_ROOT_FOLDER  fs::path(getenv("HOME")) / "111" / "sandbox_root"

#define REPLICATOR_ROOT_FOLDER_2          fs::path(getenv("HOME")) / "111" / "replicator_root_2"
#define REPLICATOR_SANDBOX_ROOT_FOLDER_2  fs::path(getenv("HOME")) / "111" / "sandbox_root_2"

#define REPLICATOR_ROOT_FOLDER_3          fs::path(getenv("HOME")) / "111" / "replicator_root_3"
#define REPLICATOR_SANDBOX_ROOT_FOLDER_3  fs::path(getenv("HOME")) / "111" / "sandbox_root_3"

#define CLIENT_WORK_FOLDER              fs::path(getenv("HOME")) / "111" / "client_work_folder"

auto clientKeyPair = sirius::crypto::KeyPair::FromPrivate(
                               sirius::crypto::PrivateKey::FromString( "0000000000010203040501020304050102030405010203040501020304050102" ));


#define REPLICATOR_PRIVATE_KEY      "1000000000010203040501020304050102030405010203040501020304050102"
#define REPLICATOR_PRIVATE_KEY_2    "2000000000010203040501020304050102030405010203040501020304050102"
#define REPLICATOR_PRIVATE_KEY_3    "3000000000010203040501020304050102030405010203040501020304050102"
#define REPLICATOR_PRIVATE_KEY_4    "4000000000010203040501020304050102030405010203040501020304050102"
#define REPLICATOR_PRIVATE_KEY_5    "5000000000010203040501020304050102030405010203040501020304050102"

#define DRIVE_PUB_KEY                   std::array<uint8_t,32>{1,0,0,0,0,5,6,7,8,9, 0,1,2,3,4,5,6,7,8,9, 0,1,2,3,4,5,6,7,8,9, 0,1}

const sirius::Hash256 downloadChannelHash1 = std::array<uint8_t,32>{1,1,1,1};
const sirius::Hash256 downloadChannelHash2 = std::array<uint8_t,32>{2,2,2,2};
const sirius::Hash256 downloadChannelHash3 = std::array<uint8_t,32>{3,3,3,3};

const sirius::Hash256 initApprovalHash = std::array<uint8_t,32>{0xf,0,0,0};

const sirius::Hash256 modifyTransactionHash1 = std::array<uint8_t,32>{0xa1,0xf,0xf,0xf};
const sirius::Hash256 modifyTransactionHash2 = std::array<uint8_t,32>{0xa2,0xf,0xf,0xf};

namespace fs = std::filesystem;

using namespace sirius::drive;

inline std::mutex gExLogMutex;

static std::string now_str();

#define EXLOG(expr) { \
        std::lock_guard<std::mutex> autolock( gExLogMutex ); \
        std::cout << now_str() << ": " << expr << std::endl << std::flush; \
    }

#define _EXLOG(expr) { \
        std::lock_guard<std::mutex> autolock( gExLogMutex ); \
        std::cout << ": " << expr << std::endl << std::flush; \
    }

// Replicators
//
std::shared_ptr<Replicator> gReplicator;
std::thread gReplicatorThread;
std::shared_ptr<Replicator> gReplicator2;
std::thread gReplicatorThread2;
std::shared_ptr<Replicator> gReplicator3;
std::thread gReplicatorThread3;
std::shared_ptr<Replicator> gReplicator4;
std::thread gReplicatorThread4;
std::shared_ptr<Replicator> gReplicator5;
std::thread gReplicatorThread5;

std::map<sirius::Key,std::shared_ptr<Replicator>> gReplicatorMap;


class MyReplicatorEventHandler;

static std::shared_ptr<Replicator> createReplicator(
                                        const sirius::crypto::KeyPair&  keyPair,
                                        std::string&&       ipAddr,
                                        int                 port,
                                        std::string&&       rootFolder,
                                        std::string&&       sandboxRootFolder,
                                        bool                useTcpSocket,
                                        MyReplicatorEventHandler& handler,
                                        const char*         dbgReplicatorName );

static void modifyDrive( std::shared_ptr<Replicator>    replicator,
                         const sirius::Key&             driveKey,
                         const sirius::Key&             clientPublicKey,
                         const InfoHash&                hash,
                         const sirius::Hash256&         transactionHash,
                         const ReplicatorList&          replicatorList,
                         uint64_t                       maxDataSize );

//
// Client functions
//
static fs::path createClientFiles( size_t bigFileSize );
static void     clientDownloadFsTree();
static void     clientModifyDrive( const ActionList& actionList,
                                   const ReplicatorList& replicatorList,
                                   const sirius::Hash256& transactionHash );
static void     clientDownloadFiles( int fileNumber, Folder& folder );

// FsTree
FsTree gFsTree;

// Client folder for his files
fs::path gClientFolder;

// Libtorrent session
std::shared_ptr<ClientSession> gClientSession;


//
// global variables, which help synchronize client and replicator
//

bool                        isDownloadCompleted = false;
InfoHash                    clientModifyHash;
std::condition_variable     clientCondVar;
std::mutex                  clientMutex;

std::shared_ptr<InfoHash>   driveRootHash;

ReplicatorList              replicatorList;

std::condition_variable     modifyCompleteCondVar;
std::atomic<int>            modifyCompleteCounter{0};
std::mutex                  modifyCompleteMutex;

// Listen (socket) error handle
//
static void clientSessionErrorHandler( const lt::alert* alert )
{
    if ( alert->type() == lt::listen_failed_alert::alert_type )
    {
        std::cerr << alert->message() << std::endl << std::flush;
        exit(-1);
    }
}

#ifdef __APPLE__
#pragma mark --MyReplicatorEventHandler--
#endif

class MyReplicatorEventHandler : public ReplicatorEventHandler, public DbgReplicatorEventHandler
{
public:

    static std::optional<ApprovalTransactionInfo>           m_approvalTransactionInfo;
    static std::optional<DownloadApprovalTransactionInfo>   m_dnApprovalTransactionInfo;
    static std::mutex                                       m_transactionInfoMutex;

    // It will be called before 'replicator' shuts down
    virtual void willBeTerminated( Replicator& replicator ) override
    {
        EXLOG( "Replicator will be terminated: " << replicator.dbgReplicatorName() );
    }

    virtual void downloadApprovalTransactionIsReady( Replicator& replicator, const DownloadApprovalTransactionInfo& info ) override
    {
        EXLOG( "downloadApprovalTransactionIsReady: " << replicator.dbgReplicatorName() );

        const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
        if ( !m_dnApprovalTransactionInfo )
        {
            m_dnApprovalTransactionInfo = { std::move(info) };
            
            std::thread( [info] { gReplicator->onDownloadApprovalTransactionHasBeenPublished( info.m_blockHash, info.m_downloadChannelId ); }).detach();
            std::thread( [info] { gReplicator2->onDownloadApprovalTransactionHasBeenPublished( info.m_blockHash, info.m_downloadChannelId ); }).detach();
            std::thread( [info] { gReplicator3->onDownloadApprovalTransactionHasBeenPublished( info.m_blockHash, info.m_downloadChannelId ); }).detach();
            
            
            for( const auto& opinion : info.m_opinions )
            {
                EXLOG( "---------------------------------------------------------" );
                EXLOG( "----  DownloadApprovalTransactionHasBeenPublished  ------" );
                EXLOG( "---------------------------------------------------------" );
                EXLOG( "download opinion of: " << gReplicatorMap[opinion.m_replicatorKey]->dbgReplicatorName() );
                for( const auto& bytes : opinion.m_downloadedBytes )
                {
                    EXLOG( "  bytes: " << bytes );
                }
            }
        }
    }

    // It will be called when rootHash is calculated in sandbox
    virtual void rootHashIsCalculated( Replicator&                    replicator,
                                       const sirius::Key&             driveKey,
                                       const sirius::drive::InfoHash& modifyTransactionHash,
                                       const sirius::drive::InfoHash& sandboxRootHash )  override
    {
        //EXLOG( "rootHshIsCalculated: " << replicator.dbgReplicatorName() );
        EXLOG( "@ sandbox calculated: " << replicator.dbgReplicatorName() );
    }
    
    // It will be called when transaction could not be completed
    virtual void modifyTransactionEndedWithError( Replicator& replicator,
                                             const sirius::Key&             driveKey,
                                             const ModifyRequest&           modifyRequest,
                                             const std::string&             reason,
                                             int                            errorCode )  override
    {
        EXLOG( "modifyTransactionEndedWithError: " << replicator.dbgReplicatorName() );
    }
    
    // It will initiate the approving of modify transaction
    virtual void modifyApprovalTransactionIsReady( Replicator& replicator, ApprovalTransactionInfo&& transactionInfo )  override
    {
        EXLOG( "modifyApprovalTransactionIsReady: " << replicator.dbgReplicatorName() );
        const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);
        
        for( const auto& opinion: transactionInfo.m_opinions )
        {
            std::cout << " key:" << int(opinion.m_replicatorKey[0]) << " ";
            for( size_t i=0; i<opinion.m_uploadReplicatorKeys.size(); i+=32  )
            {
                std::cout << int(opinion.m_uploadReplicatorKeys[i]) << ":" << opinion.m_replicatorUploadBytes[i/32] << " ";
            }
            std::cout << "client:" <<opinion.m_clientUploadBytes << std::endl;
        }
        
        gReplicator3->printTrafficDistribution( modifyTransactionHash1.array() );
        gReplicator->printTrafficDistribution( modifyTransactionHash1.array() );
        gReplicator2->printTrafficDistribution( modifyTransactionHash1.array() );

        if ( !m_approvalTransactionInfo )
        {
            m_approvalTransactionInfo = { std::move(transactionInfo) };
            
            std::thread( [] { gReplicator->onApprovalTransactionHasBeenPublished( *MyReplicatorEventHandler::m_approvalTransactionInfo ); }).detach();
            std::thread( [] { gReplicator2->onApprovalTransactionHasBeenPublished( *MyReplicatorEventHandler::m_approvalTransactionInfo ); }).detach();
            std::thread( [] { gReplicator3->onApprovalTransactionHasBeenPublished( *MyReplicatorEventHandler::m_approvalTransactionInfo ); }).detach();
        }
    }
    
    // It will initiate the approving of single modify transaction
    virtual void singleModifyApprovalTransactionIsReady( Replicator& replicator, ApprovalTransactionInfo&& transactionInfo )  override
    {
        EXLOG( "singleModifyApprovalTransactionIsReady: " << replicator.dbgReplicatorName() );
    }

    // It will be called after the drive is synchronized with sandbox
    virtual void driveModificationIsCompleted( Replicator&                    replicator,
                                               const sirius::Key&             driveKey,
                                               const sirius::drive::InfoHash& modifyTransactionHash,
                                               const sirius::drive::InfoHash& rootHash ) override
    {
        //EXLOG( "driveModificationIsCompleted: " << replicator.dbgReplicatorName() );
        EXLOG( "" );
        EXLOG( "@ update_completed:" << replicator.dbgReplicatorName() );

        driveRootHash = std::make_shared<InfoHash>( replicator.getRootHash( driveKey ) );
        EXLOG( "@ Drive modified: " << replicator.dbgReplicatorName() << "      rootHash:" << rootHash );
        
        std::lock_guard<std::mutex> autolock( gExLogMutex );
        replicator.printDriveStatus( driveKey );

        modifyCompleteCounter++;
        modifyCompleteCondVar.notify_all();
    }
};

std::optional<ApprovalTransactionInfo>          MyReplicatorEventHandler::m_approvalTransactionInfo;
std::optional<DownloadApprovalTransactionInfo>  MyReplicatorEventHandler::m_dnApprovalTransactionInfo;
std::mutex                                      MyReplicatorEventHandler::m_transactionInfoMutex;

MyReplicatorEventHandler gMyReplicatorEventHandler;
MyReplicatorEventHandler gMyReplicatorEventHandler2;
MyReplicatorEventHandler gMyReplicatorEventHandler3;
MyReplicatorEventHandler gMyReplicatorEventHandler4;
MyReplicatorEventHandler gMyReplicatorEventHandler5;


#ifdef __APPLE__
#pragma mark --main()--
#endif

//
// main
//
int main(int,char**)
{
    fs::remove_all( ROOT_TEST_FOLDER );

    auto startTime = std::clock();

    ///
    /// Make the list of replicator addresses
    ///
    boost::asio::ip::address e = boost::asio::ip::address::from_string(REPLICATOR_IP_ADDR);
    replicatorList.emplace_back( ReplicatorInfo{ {e, REPLICATOR_PORT},
        sirius::crypto::KeyPair::FromPrivate( sirius::crypto::PrivateKey::FromString( REPLICATOR_PRIVATE_KEY)).publicKey() } );
    
    boost::asio::ip::address e2 = boost::asio::ip::address::from_string(REPLICATOR_IP_ADDR_2);
    replicatorList.emplace_back( ReplicatorInfo{ {e2, REPLICATOR_PORT_2},
        sirius::crypto::KeyPair::FromPrivate( sirius::crypto::PrivateKey::FromString( REPLICATOR_PRIVATE_KEY_2)).publicKey() } );

    boost::asio::ip::address e3 = boost::asio::ip::address::from_string(REPLICATOR_IP_ADDR_3);
    replicatorList.emplace_back( ReplicatorInfo{ {e3, REPLICATOR_PORT_3},
        sirius::crypto::KeyPair::FromPrivate( sirius::crypto::PrivateKey::FromString( REPLICATOR_PRIVATE_KEY_3)).publicKey() } );

    printf( "client key[0] :      0x%x %i\n", clientKeyPair.publicKey().array()[0], clientKeyPair.publicKey().array()[0] );
    printf( "replicator1 key[0] : 0x%x %i\n", replicatorList[0].m_publicKey[0], replicatorList[0].m_publicKey[0] );
    printf( "replicator2 key[0] : 0x%x %i\n", replicatorList[1].m_publicKey[0], replicatorList[1].m_publicKey[0] );
    printf( "replicator3 key[0] : 0x%x %i\n", replicatorList[2].m_publicKey[0], replicatorList[2].m_publicKey[0] );

    ///
    /// Create replicators
    ///
    auto replicatorKeyPair   = sirius::crypto::KeyPair::FromPrivate(
                                       sirius::crypto::PrivateKey::FromString( REPLICATOR_PRIVATE_KEY ));
    auto replicatorKeyPair_2 = sirius::crypto::KeyPair::FromPrivate(
                                       sirius::crypto::PrivateKey::FromString( REPLICATOR_PRIVATE_KEY_2 ));
    auto replicatorKeyPair_3 = sirius::crypto::KeyPair::FromPrivate(
                                       sirius::crypto::PrivateKey::FromString( REPLICATOR_PRIVATE_KEY_3 ));

    gReplicator = createReplicator( replicatorKeyPair,
                                    REPLICATOR_IP_ADDR,
                                    REPLICATOR_PORT,
                                    std::string( REPLICATOR_ROOT_FOLDER ),
                                    std::string( REPLICATOR_SANDBOX_ROOT_FOLDER ),
                                    TRANSPORT_PROTOCOL,
                                    gMyReplicatorEventHandler,
                                    "replicator1" );
    gReplicatorMap[gReplicator->replicatorKey()] = gReplicator;

    gReplicator2 = createReplicator( replicatorKeyPair_2,
                                    REPLICATOR_IP_ADDR_2,
                                    REPLICATOR_PORT_2,
                                    std::string( REPLICATOR_ROOT_FOLDER_2 ),
                                    std::string( REPLICATOR_SANDBOX_ROOT_FOLDER_2 ),
                                    TRANSPORT_PROTOCOL,
                                    gMyReplicatorEventHandler2,
                                    "replicator2" );
    gReplicatorMap[gReplicator2->replicatorKey()] = gReplicator2;

    gReplicator3 = createReplicator( replicatorKeyPair_3,
                                    REPLICATOR_IP_ADDR_3,
                                    REPLICATOR_PORT_3,
                                    std::string( REPLICATOR_ROOT_FOLDER_3 ),
                                    std::string( REPLICATOR_SANDBOX_ROOT_FOLDER_3 ),
                                    TRANSPORT_PROTOCOL,
                                    gMyReplicatorEventHandler3,
                                    "replicator3" );
    gReplicatorMap[gReplicator3->replicatorKey()] = gReplicator3;

    ///
    /// Create client session
    ///
    gClientFolder  = createClientFiles(BIG_FILE_SIZE);
    gClientSession = createClientSession( std::move(clientKeyPair),
                                         CLIENT_IP_ADDR ":5000",
                                         clientSessionErrorHandler,
                                         TRANSPORT_PROTOCOL,
                                         "client" );
    _EXLOG("");
    
    // set root drive hash
    driveRootHash = std::make_shared<InfoHash>( gReplicator->getRootHash( DRIVE_PUB_KEY ) );

    fs::path clientFolder = gClientFolder / "client_files";

    /// Client: read fsTree (1)
    ///
//    TODO++
//    gClientSession->setDownloadChannel( replicatorList, downloadChannelHash1 );
//    clientDownloadFsTree();

    /// Client: request to modify drive (1)
    ///
    EXLOG( "\n# Client started: 1-st upload" );
    {
        ActionList actionList;
        actionList.push_back( Action::newFolder( "fff1/" ) );
        actionList.push_back( Action::newFolder( "fff1/ffff1" ) );
        actionList.push_back( Action::upload( clientFolder / "a.txt", "fff2/a.txt" ) );

        //actionList.push_back( Action::upload( clientFolder / "a.txt", "a.txt" ) );
        actionList.push_back( Action::upload( clientFolder / "a.txt", "a2.txt" ) );
        actionList.push_back( Action::upload( clientFolder / "b.bin", "f1/b1.bin" ) );
        actionList.push_back( Action::upload( clientFolder / "b.bin", "f2/b2.bin" ) );
        actionList.push_back( Action::upload( clientFolder / "a.txt", "f2/a.txt" ) );

        clientModifyDrive( actionList, replicatorList, modifyTransactionHash1 );
    }

    modifyCompleteCounter = 0;
    MyReplicatorEventHandler::m_approvalTransactionInfo.reset();

    gReplicatorThread  = std::thread( modifyDrive, gReplicator,  DRIVE_PUB_KEY, clientKeyPair.publicKey(), clientModifyHash, modifyTransactionHash1, replicatorList, BIG_FILE_SIZE+1024 );
    gReplicatorThread2 = std::thread( modifyDrive, gReplicator2, DRIVE_PUB_KEY, clientKeyPair.publicKey(), clientModifyHash, modifyTransactionHash1, replicatorList, BIG_FILE_SIZE+1024 );
    gReplicatorThread3 = std::thread( modifyDrive, gReplicator3, DRIVE_PUB_KEY, clientKeyPair.publicKey(), clientModifyHash, modifyTransactionHash1, replicatorList, BIG_FILE_SIZE+1024 );
    
    {
        std::unique_lock<std::mutex> lock(modifyCompleteMutex);
        modifyCompleteCondVar.wait( lock, [] { return modifyCompleteCounter == 3; } );
    }

    gReplicatorThread.join();
    gReplicatorThread2.join();
    gReplicatorThread3.join();
    
    /// Client: read changed fsTree (2)
    ///
    gClientSession->setDownloadChannel( replicatorList, downloadChannelHash2 );
    clientDownloadFsTree();

    /// Client: read files from drive
    clientDownloadFiles( 5, gFsTree );
    
    ///TODO
//    sleep(1);
//    gReplicator->sendMessage( "dnopinion", replicatorList[0].m_endpoint, "str" );
//    sleep(1);
//    usleep(1000);
    //_EXLOG( "replicatorList[0].m_endpoint" << replicatorList[0].m_endpoint );
    std::thread( [&]() { gReplicator->initiateDownloadApprovalTransactionInfo( initApprovalHash, downloadChannelHash2 ); } ).detach();
    std::thread( [&]() { gReplicator2->initiateDownloadApprovalTransactionInfo( initApprovalHash, downloadChannelHash2 ); } ).detach();
    std::thread( [&]() { gReplicator3->initiateDownloadApprovalTransactionInfo( initApprovalHash, downloadChannelHash2 ); } ).detach();
    EXLOG( "" );
    //sleep(1000);
    
    /// Client: modify drive (2)
    EXLOG( "" );
    EXLOG( "# Client started: 2-st upload/modify" );
    {
        ActionList actionList;
        actionList.push_back( Action::remove( "fff1/" ) );
        actionList.push_back( Action::remove( "fff2/" ) );

        actionList.push_back( Action::remove( "a2.txt" ) );
        actionList.push_back( Action::remove( "f1/b2.bin" ) );
        actionList.push_back( Action::remove( "f2/b2.bin" ) );
        actionList.push_back( Action::move( "f2/", "f2_renamed/" ) );
        actionList.push_back( Action::move( "f2_renamed/a.txt", "f2_renamed/a_renamed.txt" ) );
        clientModifyDrive( actionList, replicatorList, modifyTransactionHash2 );
    }

    modifyCompleteCounter = 0;
    MyReplicatorEventHandler::m_approvalTransactionInfo.reset();

    gReplicatorThread  = std::thread( modifyDrive, gReplicator,  DRIVE_PUB_KEY, clientKeyPair.publicKey(), clientModifyHash, modifyTransactionHash2, replicatorList, BIG_FILE_SIZE+1024 );
    gReplicatorThread2 = std::thread( modifyDrive, gReplicator2, DRIVE_PUB_KEY, clientKeyPair.publicKey(), clientModifyHash, modifyTransactionHash2, replicatorList, BIG_FILE_SIZE+1024 );
    gReplicatorThread3 = std::thread( modifyDrive, gReplicator3, DRIVE_PUB_KEY, clientKeyPair.publicKey(), clientModifyHash, modifyTransactionHash2, replicatorList, BIG_FILE_SIZE+1024 );

    {
        std::unique_lock<std::mutex> lock(modifyCompleteMutex);
        modifyCompleteCondVar.wait( lock, [] { return modifyCompleteCounter == 3; } );
    }

    gReplicatorThread.join();
    gReplicatorThread2.join();
    gReplicatorThread3.join();

    /// Client: read new fsTree (3)
    gClientSession->setDownloadChannel( replicatorList, downloadChannelHash3 );
    clientDownloadFsTree();

    /// Delete client session and replicators
    gClientSession.reset();
    gReplicator.reset();
    gReplicator2.reset();
    gReplicator3.reset();

    _EXLOG( "\ntotal time: " << float( std::clock() - startTime ) /  CLOCKS_PER_SEC );

    return 0;
}

//
// replicator
//
#ifdef __APPLE__
#pragma mark --replicator--
#endif

static std::shared_ptr<Replicator> createReplicator(
                                        const sirius::crypto::KeyPair&  keyPair,
                                        std::string&&       ipAddr,
                                        int                 port,
                                        std::string&&       rootFolder,
                                        std::string&&       sandboxRootFolder,
                                        bool                useTcpSocket,
                                        MyReplicatorEventHandler& handler,
                                        const char*         dbgReplicatorName )
{
    EXLOG( "creating: " << dbgReplicatorName << " with key: " <<  int(keyPair.publicKey().array()[0]) );

    auto replicator = createDefaultReplicator(
                                              std::move( keyPair ),
                                              std::move( ipAddr ),
                                              std::to_string(port),
                                              std::move( rootFolder ),
                                              std::move( sandboxRootFolder ),
                                              useTcpSocket,
                                              handler,
                                              &handler,
                                              dbgReplicatorName );

    replicator->setDownloadApprovalTransactionTimerDelay(1);
    replicator->setModifyApprovalTransactionTimerDelay(1);
    replicator->start();
    replicator->addDrive( DRIVE_PUB_KEY, {100*1024*1024, 0, replicatorList} );

    replicator->addDownloadChannelInfo( DRIVE_PUB_KEY, { downloadChannelHash1.array(), 1024*1024, replicatorList, { clientKeyPair.publicKey() }} );
    replicator->addDownloadChannelInfo( DRIVE_PUB_KEY, { downloadChannelHash2.array(), 10*1024*1024, replicatorList, { clientKeyPair.publicKey() }} );
    replicator->addDownloadChannelInfo( DRIVE_PUB_KEY, { downloadChannelHash3.array(), 1024*1024, replicatorList, { clientKeyPair.publicKey() }} );

    return replicator;
}

static void modifyDrive( std::shared_ptr<Replicator>    replicator,
                         const sirius::Key&             driveKey,
                         const sirius::Key&             clientPublicKey,
                         const InfoHash&                clientDataInfoHash,
                         const sirius::Hash256&         transactionHash,
                         const ReplicatorList&          replicatorList,
                         uint64_t                       maxDataSize )
{
    replicator->startModify( DRIVE_PUB_KEY, ModifyRequest{ clientDataInfoHash, transactionHash, maxDataSize, replicatorList, clientPublicKey } );
}

//
// clientDownloadHandler
//
static void clientDownloadHandler( download_status::code code,
                                   const InfoHash& infoHash,
                                   const std::filesystem::path /*filePath*/,
                                   size_t /*downloaded*/,
                                   size_t /*fileSize*/,
                                   const std::string& /*errorText*/ )
{
    if ( code == download_status::download_complete )
    {
        EXLOG( "# Client received FsTree: " << toString(infoHash) );
        EXLOG( "# FsTree file path: " << gClientFolder / "fsTree-folder" / FS_TREE_FILE_NAME );
        gFsTree.deserialize( gClientFolder / "fsTree-folder" / FS_TREE_FILE_NAME );

        // print FsTree
        {
            std::lock_guard<std::mutex> autolock( gExLogMutex );
            gFsTree.dbgPrint();
        }

        isDownloadCompleted = true;
        clientCondVar.notify_all();
    }
    else if ( code == download_status::failed )
    {
        exit(-1);
    }
}

//
// clientDownloadFsTree
//
static void clientDownloadFsTree()
{
    InfoHash rootHash = *driveRootHash;
    driveRootHash.reset();

    isDownloadCompleted = false;

    LOG("");
    EXLOG( "# Client started FsTree download: " << toString(rootHash) );

    gClientSession->download( DownloadContext(
                                    DownloadContext::fs_tree,
                                    clientDownloadHandler,                                    
                                    rootHash,
                                    *gClientSession->downloadChannelId(), 0 ),
                                    gClientFolder / "fsTree-folder" );

    /// wait the end of file downloading
    {
        std::unique_lock<std::mutex> lock(clientMutex);
        clientCondVar.wait( lock, [] { return isDownloadCompleted; } );
    }
}

//
// clientModifyDrive
//
static void clientModifyDrive( const ActionList& actionList,
                               const ReplicatorList& replicatorList,
                               const sirius::Hash256& transactionHash )
{
    {
        std::lock_guard<std::mutex> autolock( gExLogMutex );
        actionList.dbgPrint();
    }

    // Create empty tmp folder for 'client modify data'
    //
    auto tmpFolder = fs::temp_directory_path() / "modify_drive_data";
    fs::remove_all( tmpFolder );
    fs::create_directories( tmpFolder );

    // start file uploading
    InfoHash hash = gClientSession->addActionListToSession( actionList, replicatorList, transactionHash, tmpFolder );

    // inform replicator
    clientModifyHash = hash;

    EXLOG( "# Client is waiting the end of replicator update" );
}

//
// clientDownloadFilesHandler
//
int downloadFileCount;
int downloadedFileCount;
static void clientDownloadFilesHandler( download_status::code code,
                                        const InfoHash& /*infoHash*/,
                                        const std::filesystem::path filePath,
                                        size_t downloaded,
                                        size_t fileSize,
                                        const std::string& errorText )
{
    if ( code == download_status::download_complete )
    {
//        LOG( "@ hash: " << toString(context.m_infoHash) );
//        LOG( "@ renameAs: " << context.m_renameAs );
//        LOG( "@ saveFolder: " << context.m_saveFolder );
        if ( ++downloadedFileCount == downloadFileCount )
        {
            EXLOG( "# Downloaded " << filePath << " files" );
            isDownloadCompleted = true;
            clientCondVar.notify_all();
        }
    }
    else if ( code == download_status::downloading )
    {
        //LOG( "downloading: " << downloaded << " of " << fileSize );
    }
    else if ( code == download_status::failed )
    {
        EXLOG( "# Error in clientDownloadFilesHandler: " << errorText );
        exit(-1);
    }
}

//
// Client: read files
//
static void clientDownloadFilesR( const Folder& folder )
{
    for( const auto& child: folder.childs() )
    {
        if ( isFolder(child) )
        {
            clientDownloadFilesR( getFolder(child) );
        }
        else
        {
            const File& file = getFile(child);
            std::string folderName = "root";
            if ( folder.name() != "/" )
                folderName = folder.name();
            EXLOG( "# Client started download file " << hashToFileName( file.hash() ) );
            EXLOG( "#  to " << gClientFolder / "downloaded_files" / folderName  / file.name() );
            gClientSession->download( DownloadContext(
                                            DownloadContext::file_from_drive,
                                            clientDownloadFilesHandler,
                                            file.hash(),
                                            {}, 0,
                                            gClientFolder / "downloaded_files" / folderName / file.name() ),
                                            //gClientFolder / "downloaded_files" / folderName / toString(file.hash()) ),
                                            gClientFolder / "downloaded_files" );
        }
    }
}
static void clientDownloadFiles( int fileNumber, Folder& fsTree )
{
    isDownloadCompleted = false;

    downloadFileCount = 0;
    downloadedFileCount = 0;
    fsTree.iterate([](File& /*file*/) {
        downloadFileCount++;
    });

    if ( downloadFileCount == 0 )
    {
        EXLOG( "downloadFileCount == 0" );
        return;
    }

    if ( fileNumber != downloadFileCount )
    {
        EXLOG( "!ERROR! clientDownloadFiles(): fileNumber != downloadFileCount; " << fileNumber <<"!=" << downloadFileCount );
        exit(-1);
    }

    EXLOG("#======================client start downloading=== " << downloadFileCount );

    clientDownloadFilesR( fsTree );

    /// wait the end of file downloading
    {
        std::unique_lock<std::mutex> lock(clientMutex);
        clientCondVar.wait( lock, [] { return isDownloadCompleted; } );
    }
}


//
// createClientFiles
//
static fs::path createClientFiles( size_t bigFileSize ) {

    // Create empty tmp folder for testing
    //
    auto dataFolder = CLIENT_WORK_FOLDER / "client_files";
    fs::remove_all( dataFolder.parent_path() );
    fs::create_directories( dataFolder );
    //fs::create_directories( dataFolder/"empty_folder" );

    {
        std::ofstream file( dataFolder / "a.txt" );
        file.write( "a_txt", 5 );
    }
    {
        fs::path b_bin = dataFolder / "b.bin";
        fs::create_directories( b_bin.parent_path() );
//        std::vector<uint8_t> data(10*1024*1024);
        std::vector<uint8_t> data(bigFileSize);
        std::generate( data.begin(), data.end(), std::rand );
        std::ofstream file( b_bin );
        file.write( (char*) data.data(), data.size() );
    }
    {
        std::ofstream file( dataFolder / "c.txt" );
        file.write( "c_txt", 5 );
    }
    {
        std::ofstream file( dataFolder / "d.txt" );
        file.write( "d_txt", 5 );
    }

    // Return path to file
    return dataFolder.parent_path();
}


static std::string now_str()
{
    // Get current time from the clock, using microseconds resolution
    const boost::posix_time::ptime now =
        boost::posix_time::microsec_clock::local_time();

    // Get the time offset in current day
    const boost::posix_time::time_duration td = now.time_of_day();

    //
    // Extract hours, minutes, seconds and milliseconds.
    //
    // Since there is no direct accessor ".milliseconds()",
    // milliseconds are computed _by difference_ between total milliseconds
    // (for which there is an accessor), and the hours/minutes/seconds
    // values previously fetched.
    //
    const long hours        = td.hours();
    const long minutes      = td.minutes();
    const long seconds      = td.seconds();
    const long milliseconds = td.total_milliseconds() -
                              ((hours * 3600 + minutes * 60 + seconds) * 1000);

    //
    // Format like this:
    //
    //      hh:mm:ss.SS
    //
    // e.g. 02:15:40:321
    //
    //      ^          ^
    //      |          |
    //      123456789*12
    //      ---------10-     --> 12 chars + \0 --> 13 chars should suffice
    //
    //
    char buf[40];
    sprintf(buf, "%02ld:%02ld:%02ld.%03ld",
        hours, minutes, seconds, milliseconds);

    return buf;
}


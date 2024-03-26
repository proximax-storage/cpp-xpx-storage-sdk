#include "types.h"
#include "drive/ClientSession.h"
#include "drive/StreamerSession.h"
#include "drive/ViewerSession.h"
#include "drive/Replicator.h"
#include "drive/FlatDrive.h"
#include "drive/FsTree.h"
#include "drive/Utils.h"
#include "crypto/Signer.h"

#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <future>
#include <condition_variable>
#include "boost/date_time/posix_time/posix_time.hpp"

#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/kademlia/ed25519.hpp>

#include <sirius_drive/session_delegate.h>

//(???+) !!!
const bool testLateReplicator = false;
const bool gRestartReplicators = true;
const bool testSmallModifyDataSize = true;
bool gBreak_On_Warning = true;

//
// This example shows interaction between 'client' and 'replicator'.
//

#define BIG_FILE_SIZE       1 * 1024*1024 //150//4
#define MODIFY_DATA_SIZE    (BIG_FILE_SIZE)-32000

#define TRANSPORT_PROTOCOL false // true - TCP, false - uTP

// !!!
// CLIENT_IP_ADDR should be changed to proper address according to your network settings (see ifconfig)

#define CLIENT_IP_ADDR          "192.168.20.30"
#define CLIENT_PORT             ":3000"
#define CLIENT_IP_ADDR1         "192.168.20.31"
#define CLIENT_PORT1            ":3001"
#define CLIENT_IP_ADDR2         "192.168.20.32"
#define CLIENT_PORT2            ":3002"

#define REPLICATOR_IP_ADDR      "192.168.20.21"
#define REPLICATOR_PORT         2001
#define REPLICATOR_IP_ADDR_2    "192.168.20.22"
#define REPLICATOR_PORT_2       2002
#define REPLICATOR_IP_ADDR_3    "192.168.20.23"
#define REPLICATOR_PORT_3       2003
#define REPLICATOR_IP_ADDR_4    "192.168.20.24"
#define REPLICATOR_PORT_4       2004

//#define OSB_OUTPUT_PLAYLIST             fs::path(getenv("HOME")) / "111" / "stream" / "stream.m3u8"
#define OSB_OUTPUT_PLAYLIST             fs::path(getenv("HOME")) / "000" / "111-stream" / "obs-stream.m3u8"

#define ROOT_TEST_FOLDER                fs::path(getenv("HOME")) / "111"
#define REPLICATOR_ROOT_FOLDER          fs::path(getenv("HOME")) / "111" / "replicator_root"
#define REPLICATOR_SANDBOX_ROOT_FOLDER  fs::path(getenv("HOME")) / "111" / "sandbox_root"

#define REPLICATOR_ROOT_FOLDER_2          fs::path(getenv("HOME")) / "111" / "replicator_root_2"
#define REPLICATOR_SANDBOX_ROOT_FOLDER_2  fs::path(getenv("HOME")) / "111" / "sandbox_root_2"

#define REPLICATOR_ROOT_FOLDER_3          fs::path(getenv("HOME")) / "111" / "replicator_root_3"
#define REPLICATOR_SANDBOX_ROOT_FOLDER_3  fs::path(getenv("HOME")) / "111" / "sandbox_root_3"

#define REPLICATOR_ROOT_FOLDER_4          fs::path(getenv("HOME")) / "111" / "replicator_root_4"
#define REPLICATOR_SANDBOX_ROOT_FOLDER_4  fs::path(getenv("HOME")) / "111" / "sandbox_root_4"

#define CLIENT_WORK_FOLDER                fs::path(getenv("HOME")) / "111" / "client_work_folder"
#define VIEWER_STREAM_ROOT_FOLDER         fs::path(getenv("HOME")) / "111" / "viewer_stream_root_folder"
#define STREAMER_WORK_FOLDER              fs::path(getenv("HOME")) / "111" / "streamer_folder"

auto clientKeyPair = sirius::crypto::KeyPair::FromPrivate(
        sirius::crypto::PrivateKey::FromString( "0000000000010203040501020304050102030405010203040501020304050102" ));
auto clientKeyPair1 = sirius::crypto::KeyPair::FromPrivate(
        sirius::crypto::PrivateKey::FromString( "0000000001110203040501020304050102030405010203040501020304050102" ));
auto viewerKeyPair = sirius::crypto::KeyPair::FromPrivate(
        sirius::crypto::PrivateKey::FromString( "0000000002210203040501020304050102030405010203040501020304050102" ));


#define REPLICATOR_PRIVATE_KEY      "1000000000010203040501020304050102030405010203040501020304050102"
#define REPLICATOR_PRIVATE_KEY_2    "2000000000010203040501020304050102030405010203040501020304050102"
#define REPLICATOR_PRIVATE_KEY_3    "3000000000010203040501020304050102030405010203040501020304050102"
#define REPLICATOR_PRIVATE_KEY_4    "4000000000010203040501020304050102030405010203040501020304050102"
#define REPLICATOR_PRIVATE_KEY_5    "5000000000010203040501020304050102030405010203040501020304050102"

#define DRIVE_PUB_KEY                   std::array<uint8_t,32>{1,0,0,0,0,5,6,7,8,9, 0,1,2,3,4,5,6,7,8,9, 0,1,2,3,4,5,6,7,8,9, 0,1}

const sirius::Hash256 downloadChannelHash1 = std::array<uint8_t,32>{1,1,1,1};
const sirius::Hash256 downloadChannelHash2 = std::array<uint8_t,32>{2,2,2,2};
const sirius::Hash256 downloadChannelHash3 = std::array<uint8_t,32>{3,3,3,3};
const sirius::Hash256 verifyTx             = std::array<uint8_t,32>{8,8,8,8};

const sirius::Hash256 initApprovalHash = std::array<uint8_t,32>{0xf,0,0,0};

const sirius::Hash256 modifyTransactionHash1  = std::array<uint8_t,32>{0xa1,0xf,0xf,0xf};
const sirius::Hash256 modifyTransactionHash1b = std::array<uint8_t,32>{0xab,0xf,0xf,0xf};
const sirius::Hash256 modifyTransactionHash2  = std::array<uint8_t,32>{0xa2,0xf,0xf,0xf};

// streamId
const sirius::Hash256 streamTx  = std::array<uint8_t,32>{0xee,0xee,0xee,0xee};


namespace fs = std::filesystem;

using namespace sirius::drive;

inline std::mutex gExLogMutex;

#define EXLOG(expr) { \
        __LOG( "+++ exlog: " << expr << std::endl << std::flush); \
    }

#define _EXLOG(expr) { \
        std::lock_guard<std::mutex> autolock( gExLogMutex ); \
        __LOG( "+++ exlog: " <<  expr << std::endl << std::flush); \
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
        const sirius::crypto::KeyPair&      keyPair,
        std::string&&                       ipAddr,
        int                                 port,
        std::string&&                       rootFolder,
        std::string&&                       sandboxRootFolder,
        bool                                useTcpSocket,
        const std::vector<ReplicatorInfo>&  bootstraps,
        MyReplicatorEventHandler&           handler,
        const std::string&                  dbgReplicatorName );


//
// Client functions
//
static fs::path createClientFiles( size_t bigFileSize );

// FsTree
FsTree gFsTree;

// Client folder for his files
fs::path gClientFolder;

// Libtorrent sessions
//std::shared_ptr<ClientSession>      gClientSession;
//std::shared_ptr<ClientSession>      gClientSession1;
std::shared_ptr<ViewerSession>      gStreamerSession;
std::shared_ptr<ViewerSession>      gViewerSession;


//
// global variables, which help synchronize client and replicator
//

bool                        isDownloadCompleted = false;
InfoHash                    clientModifyHash;
std::condition_variable     clientCondVar;
std::mutex                  clientMutex;

std::shared_ptr<InfoHash>   driveRootHash;

ReplicatorList              replicatorList;
endpoint_list               endpointList;

std::condition_variable     modifyCompleteCondVar;
std::atomic<int>            modifyCompleteCounter{0};
std::mutex                  modifyCompleteMutex;

std::condition_variable     verifyCompleteCondVar;
std::atomic<int>            verifyCompleteCounter{0};
std::mutex                  verifyCompleteMutex;

std::condition_variable     downloadStreamCondVar;
bool                        downloadStreamEnded{ false };
std::mutex                  downloadStreamMutex;

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

    static std::optional<PublishedModificationApprovalTransactionInfo> m_approvalTransactionInfo;
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

            std::thread( [info] { gReplicator->asyncDownloadApprovalTransactionHasBeenPublished( info.m_blockHash, info.m_downloadChannelId ); }).detach();
            std::thread( [info] { gReplicator2->asyncDownloadApprovalTransactionHasBeenPublished( info.m_blockHash, info.m_downloadChannelId ); }).detach();
            std::thread( [info] { gReplicator3->asyncDownloadApprovalTransactionHasBeenPublished( info.m_blockHash, info.m_downloadChannelId ); }).detach();
            
            
            for( const auto& opinion : info.m_opinions )
            {
                EXLOG( "---------------------------------------------------------" );
                EXLOG( "----  DownloadApprovalTransactionHasBeenPublished  ------" );
                EXLOG( "---------------------------------------------------------" );
                EXLOG( "download opinion of: " << gReplicatorMap[opinion.m_replicatorKey]->dbgReplicatorName() );
                for( const auto& downloadInfo : opinion.m_downloadLayout )
                {
                    EXLOG( "  key: " << (int)downloadInfo.m_key[0] << ": " << downloadInfo.m_uploadedBytes );
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
                                                  const ModificationRequest&     modifyRequest,
                                                  const std::string&             reason,
                                                  int                            errorCode )  override
    {
        EXLOG( "modifyTransactionEndedWithError: " << replicator.dbgReplicatorName() );
    }

    // It will initiate the approving of modify transaction
    virtual void modifyApprovalTransactionIsReady( Replicator& replicator, const ApprovalTransactionInfo& transactionInfo )  override
    {
        EXLOG( "@ modifyApprovalTransactionIsReady: " << replicator.dbgReplicatorName() );
        const std::unique_lock<std::mutex> lock(m_transactionInfoMutex);

        for( const auto& opinion: transactionInfo.m_opinions )
        {
            std::cout << " key:" << int(opinion.m_replicatorKey[0]) << " ";
            for( size_t i=0; i<opinion.m_uploadLayout.size(); i++  )
            {
                std::cout << int(opinion.m_uploadLayout[i].m_key[0]) << ":" << opinion.m_uploadLayout[i].m_uploadedBytes << " ";
            }
        }
        
        gReplicator3->dbgPrintTrafficDistribution( modifyTransactionHash1.array() );
        gReplicator->dbgPrintTrafficDistribution( modifyTransactionHash1.array() );
        gReplicator2->dbgPrintTrafficDistribution( modifyTransactionHash1.array() );

        if ( !m_approvalTransactionInfo )
        {
            m_approvalTransactionInfo = { std::move(transactionInfo) };

            std::thread( [] { gReplicator->asyncApprovalTransactionHasBeenPublished( std::make_unique<PublishedModificationApprovalTransactionInfo>(MyReplicatorEventHandler::m_approvalTransactionInfo.value()) ); }).detach();
            std::thread( [] { gReplicator2->asyncApprovalTransactionHasBeenPublished( std::make_unique<PublishedModificationApprovalTransactionInfo>(MyReplicatorEventHandler::m_approvalTransactionInfo.value()) ); }).detach();
            std::thread( [] { gReplicator3->asyncApprovalTransactionHasBeenPublished( std::make_unique<PublishedModificationApprovalTransactionInfo>(MyReplicatorEventHandler::m_approvalTransactionInfo.value()) ); }).detach();
            std::thread( [] { gReplicator4->asyncApprovalTransactionHasBeenPublished( std::make_unique<PublishedModificationApprovalTransactionInfo>(MyReplicatorEventHandler::m_approvalTransactionInfo.value()) ); }).detach();
        }
    }

    // It will initiate the approving of single modify transaction
    virtual void singleModifyApprovalTransactionIsReady( Replicator& replicator, const ApprovalTransactionInfo& transactionInfo )  override
    {
        EXLOG( "@ singleModifyApprovalTransactionIsReady: " << replicator.dbgReplicatorName() );
    }

    void verificationTransactionIsReady( Replicator&                    replicator,
                                         const VerifyApprovalTxInfo&    transactionInfo ) override
    {
        EXLOG( "" );
        EXLOG( "@ verification_is_ready:" << replicator.dbgReplicatorName() );

        {
            std::unique_lock<std::mutex> lock(verifyCompleteMutex);
            verifyCompleteCounter++;
        }
        verifyCompleteCondVar.notify_all();
    }

    // It will be called after the drive is synchronized with sandbox
    virtual void driveModificationIsCompleted( Replicator&                    replicator,
                                               const sirius::Key&             driveKey,
                                               const sirius::drive::InfoHash& modifyTransactionHash,
                                               const sirius::drive::InfoHash& rootHash ) override
    {
        std::thread( [=,&replicator] {
            //EXLOG( "driveModificationIsCompleted: " << replicator.dbgReplicatorName() );
            EXLOG( "" );
            EXLOG( "@ update_completed:" << replicator.dbgReplicatorName() );

            std::lock_guard<std::mutex> autolock( gExLogMutex );
            replicator.dbgPrintDriveStatus( driveKey );

            modifyCompleteCounter++;

            driveRootHash = std::make_shared<InfoHash>( replicator.dbgGetRootHash( driveKey.array() ) );
            EXLOG( "@ Drive modified: modifyCompleteCounter=" << modifyCompleteCounter << ": " << replicator.dbgReplicatorName() << "      rootHash:" << rootHash );

            modifyCompleteCondVar.notify_all();

        }).detach();
    }

    virtual void opinionHasBeenReceived(  Replicator& replicator,
                                          const ApprovalTransactionInfo& opinion ) override
    {
        replicator.asyncOnOpinionReceived( opinion );
    }

    virtual void downloadOpinionHasBeenReceived(  Replicator& replicator,
                                                  const DownloadApprovalTransactionInfo& opinion ) override
    {
        replicator.asyncOnDownloadOpinionReceived( std::make_unique<DownloadApprovalTransactionInfo>(opinion) );
    }
    
    // It will be called when rootHash is calculated in sandbox
    virtual void driveIsInitialized( Replicator&                    replicator,
                                     const sirius::Key&             driveKey,
                                     const sirius::drive::InfoHash& rootHash ) override
    {
        //EXLOG( "@ driveIsInitialized: " << replicator.dbgReplicatorName() );
    }
};

std::optional<PublishedModificationApprovalTransactionInfo> MyReplicatorEventHandler::m_approvalTransactionInfo;
std::optional<DownloadApprovalTransactionInfo>  MyReplicatorEventHandler::m_dnApprovalTransactionInfo;
std::mutex                                      MyReplicatorEventHandler::m_transactionInfoMutex;

MyReplicatorEventHandler gMyReplicatorEventHandler;
MyReplicatorEventHandler gMyReplicatorEventHandler2;
MyReplicatorEventHandler gMyReplicatorEventHandler3;
MyReplicatorEventHandler gMyReplicatorEventHandler4;
MyReplicatorEventHandler gMyReplicatorEventHandler5;

auto replicatorKeyPair   = sirius::crypto::KeyPair::FromPrivate(
        sirius::crypto::PrivateKey::FromString( REPLICATOR_PRIVATE_KEY ));
auto replicatorKeyPair_2 = sirius::crypto::KeyPair::FromPrivate(
        sirius::crypto::PrivateKey::FromString( REPLICATOR_PRIVATE_KEY_2 ));
auto replicatorKeyPair_3 = sirius::crypto::KeyPair::FromPrivate(
        sirius::crypto::PrivateKey::FromString( REPLICATOR_PRIVATE_KEY_3 ));
auto replicatorKeyPair_4 = sirius::crypto::KeyPair::FromPrivate(
        sirius::crypto::PrivateKey::FromString( REPLICATOR_PRIVATE_KEY_4 ));

///
/// Create replicators
///
void createReplicators(const std::vector<ReplicatorInfo>&  bootstraps)
{

    std::thread t1( [=] {
        gReplicator = createReplicator( replicatorKeyPair,
                                        REPLICATOR_IP_ADDR,
                                        REPLICATOR_PORT,
                                        std::string( REPLICATOR_ROOT_FOLDER ),
                                        std::string( REPLICATOR_SANDBOX_ROOT_FOLDER ),
                                        TRANSPORT_PROTOCOL,
                                        bootstraps,
                                        gMyReplicatorEventHandler,
                                        "replicator1" );
    });

    std::thread t2( [=] {
        gReplicator2 = createReplicator( replicatorKeyPair_2,
                                        REPLICATOR_IP_ADDR_2,
                                        REPLICATOR_PORT_2,
                                        std::string( REPLICATOR_ROOT_FOLDER_2 ),
                                        std::string( REPLICATOR_SANDBOX_ROOT_FOLDER_2 ),
                                        TRANSPORT_PROTOCOL,
                                        bootstraps,
                                        gMyReplicatorEventHandler2,
                                        "replicator2" );
    });
    
    std::thread t3( [=] {
        gReplicator3 = createReplicator( replicatorKeyPair_3,
                                        REPLICATOR_IP_ADDR_3,
                                        REPLICATOR_PORT_3,
                                        std::string( REPLICATOR_ROOT_FOLDER_3 ),
                                        std::string( REPLICATOR_SANDBOX_ROOT_FOLDER_3 ),
                                        TRANSPORT_PROTOCOL,
                                        bootstraps,
                                        gMyReplicatorEventHandler3,
                                        "replicator3" );
    });
    
    std::thread t4( [=] {
        gReplicator4 = createReplicator( replicatorKeyPair_4,
                                        REPLICATOR_IP_ADDR_4,
                                        REPLICATOR_PORT_4,
                                        std::string( REPLICATOR_ROOT_FOLDER_4 ),
                                        std::string( REPLICATOR_SANDBOX_ROOT_FOLDER_4 ),
                                        TRANSPORT_PROTOCOL,
                                        bootstraps,
                                        gMyReplicatorEventHandler4,
                                        "replicator4" );
    });
    
    t1.join();
    t2.join();
    t3.join();
    t4.join();

    gReplicatorMap[gReplicator->dbgReplicatorKey()] = gReplicator;
    gReplicatorMap[gReplicator2->dbgReplicatorKey()] = gReplicator2;
    gReplicatorMap[gReplicator3->dbgReplicatorKey()] = gReplicator3;
    gReplicatorMap[gReplicator4->dbgReplicatorKey()] = gReplicator4;

}

endpoint_list bootstrapEndpoints;

//
// creayeRplicator
//
#ifdef __APPLE__
#pragma mark --replicator--
#endif

static std::shared_ptr<Replicator> createReplicator(
        const sirius::crypto::KeyPair&      keyPair,
        std::string&&                       ipAddr,
        int                                 port,
        std::string&&                       rootFolder,
        std::string&&                       sandboxRootFolder,
        bool                                useTcpSocket,
        const std::vector<ReplicatorInfo>&  bootstraps,
        MyReplicatorEventHandler&           handler,
        const std::string&                  dbgReplicatorName )
{
    EXLOG( "creating: " << dbgReplicatorName << " with key: " <<  int(keyPair.publicKey().array()[0]) );

    auto replicator = createDefaultReplicator(
            std::move( keyPair ),
            std::move( ipAddr ),
            std::to_string(port),
            std::move( rootFolder ),
            bootstraps,
            useTcpSocket,
            handler,
            &handler,
            dbgReplicatorName, "" );

    replicator->setDownloadApprovalTransactionTimerDelay(1);
    replicator->setModifyApprovalTransactionTimerDelay(1);
    replicator->setVerifyCodeTimerDelay(100);
    replicator->setVerifyApprovalTransactionTimerDelay(1);
    replicator->start();
//    replicator->asyncAddDrive( DRIVE_PUB_KEY, AddDriveRequest{100,         0, replicatorList, clientKeyPair.publicKey(), replicatorList, replicatorList } );
    replicator->asyncAddDrive( DRIVE_PUB_KEY, std::make_unique<AddDriveRequest>(AddDriveRequest{100*1024*1024, 0, {}, replicatorList, clientKeyPair.publicKey(), replicatorList, replicatorList} ) );

    replicator->asyncAddDownloadChannelInfo( DRIVE_PUB_KEY, std::make_unique<DownloadRequest>(DownloadRequest{ downloadChannelHash1.array(), 1024*1024*1024, replicatorList, { viewerKeyPair.publicKey() }}) );

    return replicator;
}

//
// createClientFiles
//
static fs::path createClientFiles( size_t bigFileSize )
{
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
        std::vector<uint8_t> data(bigFileSize);
        //std::generate( data.begin(), data.end(), std::rand );
        uint8_t counter=0;
        std::generate( data.begin(), data.end(), [&] { return counter++;} );
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

std::vector<std::shared_ptr<Replicator>> gReplicatorArray;

#ifdef __APPLE__
#pragma mark --main()--
#endif

//
// main
//
int main(int,char**)
{
    EXLOG(std::filesystem::current_path());
    
    gBreakOnWarning = gBreak_On_Warning;
    
    fs::remove_all( ROOT_TEST_FOLDER );

    auto startTime = std::clock();

    ///
    /// Make the list of replicator addresses
    ///
    replicatorList.emplace_back( sirius::crypto::KeyPair::FromPrivate( sirius::crypto::PrivateKey::FromString( REPLICATOR_PRIVATE_KEY)).publicKey() );
    replicatorList.emplace_back( sirius::crypto::KeyPair::FromPrivate( sirius::crypto::PrivateKey::FromString( REPLICATOR_PRIVATE_KEY_2)).publicKey() );
    replicatorList.emplace_back( sirius::crypto::KeyPair::FromPrivate( sirius::crypto::PrivateKey::FromString( REPLICATOR_PRIVATE_KEY_3)).publicKey() );
    replicatorList.emplace_back( sirius::crypto::KeyPair::FromPrivate( sirius::crypto::PrivateKey::FromString( REPLICATOR_PRIVATE_KEY_4)).publicKey() );

    boost::asio::ip::address e1 = boost::asio::ip::make_address(REPLICATOR_IP_ADDR);
    boost::asio::ip::address e2 = boost::asio::ip::make_address(REPLICATOR_IP_ADDR_2);
    boost::asio::ip::address e3 = boost::asio::ip::make_address(REPLICATOR_IP_ADDR_3);
    boost::asio::ip::address e4 = boost::asio::ip::make_address(REPLICATOR_IP_ADDR_4);
    endpointList.push_back( {e1, REPLICATOR_PORT} );
    endpointList.push_back( {e2, REPLICATOR_PORT_2} );
    endpointList.push_back( {e3, REPLICATOR_PORT_3} );
    endpointList.push_back( {e4, REPLICATOR_PORT_4} );

    printf( "replicator1 key[0] : 0x%x %i\n", replicatorList[0][0], replicatorList[0][0] );
    printf( "replicator2 key[0] : 0x%x %i\n", replicatorList[1][0], replicatorList[1][0] );
    printf( "replicator3 key[0] : 0x%x %i\n", replicatorList[2][0], replicatorList[2][0] );
    printf( "streamer    key[0] : 0x%x %i\n", clientKeyPair.publicKey()[0], clientKeyPair.publicKey()[0] );
    printf( "viewer      key[0] : 0x%x %i\n", viewerKeyPair.publicKey()[0], viewerKeyPair.publicKey()[0] );
    EXLOG( "@@@ streamer: " << clientKeyPair.publicKey() )
    EXLOG( "@@@ viewer:   " << viewerKeyPair.publicKey() )


    ///
    /// Create bootstrapEndpoints
    ///

//    std::vector<ReplicatorInfo> bootstraps = { { { boost::asio::ip::make_address(REPLICATOR_IP_ADDR_2), REPLICATOR_PORT_2 },
//                                                 replicatorKeyPair_2.publicKey() } };
    std::vector<ReplicatorInfo> bootstraps = {
        { { boost::asio::ip::make_address(REPLICATOR_IP_ADDR), REPLICATOR_PORT }, replicatorKeyPair.publicKey() },
        { { boost::asio::ip::make_address(REPLICATOR_IP_ADDR_2), REPLICATOR_PORT_2 }, replicatorKeyPair_2.publicKey() },
        { { boost::asio::ip::make_address(REPLICATOR_IP_ADDR_3), REPLICATOR_PORT_3 }, replicatorKeyPair_3.publicKey() },
        { { boost::asio::ip::make_address(REPLICATOR_IP_ADDR_4), REPLICATOR_PORT_4 }, replicatorKeyPair_4.publicKey() }
    };
    for ( const auto& bootstrap: bootstraps )
    {
        bootstrapEndpoints.push_back( bootstrap.m_endpoint );
    }

    ///
    /// Create replicators
    ///
    createReplicators( bootstraps );
    gReplicatorArray.push_back( gReplicator );
    gReplicatorArray.push_back( gReplicator2 );
    gReplicatorArray.push_back( gReplicator3 );
    gReplicatorArray.push_back( gReplicator4 );

    ///
    /// Create client sessions
    ///
    gClientFolder  = createClientFiles(BIG_FILE_SIZE);

    gStreamerSession = createViewerSession( clientKeyPair,
                                              CLIENT_IP_ADDR CLIENT_PORT,
                                              clientSessionErrorHandler,
                                              bootstrapEndpoints,
                                              TRANSPORT_PROTOCOL,
                                              "streamer" );
    
    //gStreamerSession->dbgAddReplicatorList( bootstraps );

    gViewerSession = createViewerSession( viewerKeyPair,
                                          CLIENT_IP_ADDR1 CLIENT_PORT1,
                                          clientSessionErrorHandler,
                                          bootstrapEndpoints,
                                          TRANSPORT_PROTOCOL,
                                          "viewer" );

    //gViewerSession->dbgAddReplicatorList( bootstraps );
    _EXLOG("");

    //
    // Prepare Clients
    //
    if ( ! fs::exists( fs::path(OSB_OUTPUT_PLAYLIST).parent_path() ) )
    {
        fs::create_directories( fs::path(OSB_OUTPUT_PLAYLIST).parent_path() );
    }
    fs::remove_all( STREAMER_WORK_FOLDER / "streamFolder" );
    gStreamerSession->initStream( streamTx,
                                 "streamN1",
                                 DRIVE_PUB_KEY,
                                 OSB_OUTPUT_PLAYLIST,
                                 STREAMER_WORK_FOLDER / "streamFolder",
                                 STREAMER_WORK_FOLDER / "streamFolder",
                                 [](const std::string&){},
                                 endpointList );

    
    sleep(1);
    auto endStreamBackCall = []( const sirius::Key& driveKey, const sirius::drive::InfoHash& streamId, const sirius::drive::InfoHash& actionListHash, uint64_t streamBytes  )
    {
        for( auto replicator : gReplicatorArray )
        {
            replicator->asyncFinishStreamTxPublished( driveKey, std::make_unique<StreamFinishRequest>(StreamFinishRequest{ streamId.array(), actionListHash, streamBytes }) );
        }
    };
    
    gStreamerSession->finishStream( endStreamBackCall, replicatorList, STREAMER_WORK_FOLDER / "sandboxFolder", endpointList );
    
    
    downloadStreamEnded = false;
//    auto progress = []( std::string playListPath, int chunkIndex, int chunkNumber, std::string error )
//    {
//        EXLOG( "@@@ chunkIndex,chunkNumber: " << chunkIndex << "," << chunkNumber )
//        if ( chunkIndex == chunkNumber )
//        {
//            //std::unique_lock<std::mutex> lock(downloadStreamMutex);
//            downloadStreamEnded = true;
//            downloadStreamCondVar.notify_all();
//            downloadStreamCondVar.notify_one();
//        }
//    };
    
//    //
//    // startWatchingLiveStream
//    //
//#ifdef __APPLE__
//    auto startPlayer = []( std::string address )
//    {
//        std::cout << "startPlayer address: " << address <<std::endl;
//        auto cmd = std::string( "open -a /Users/alex/Applications/VLC.app/Contents/MacOS/VLC " + address + "");
//        system( cmd.c_str() );
//    };
//#else
//    auto startPlayer = []( std::string address )
//    {
//        std::cout << "startPlayer() not implemented";
//        assert(0);
//    };
//#endif

//    gViewerSession->setDownloadChannel( replicatorList, downloadChannelHash1 );
//    gViewerSession->startWatchingLiveStream( streamTx,
//                                            gStreamerSession->publicKey(),
//                                            DRIVE_PUB_KEY, CLIENT_WORK_FOLDER / "streamFolder",
//                                            endpointList,
//                                            startPlayer,
//                                            {"localhost","5151"},
//                                            progress );

    const uint64_t maxStreamSize = 1024*1024*1024;
    StreamRequest streamRequest{ streamTx, clientKeyPair.publicKey(), "streamN1", maxStreamSize, replicatorList };
    
    for( auto replicator : gReplicatorArray )
    {
        replicator->asyncStartStream( DRIVE_PUB_KEY, std::make_unique<StreamRequest>(streamRequest) );
    }

    sleep(10000);
    gViewerSession->addReplicatorList( replicatorList );
    gViewerSession->requestStreamStatus( DRIVE_PUB_KEY, replicatorList,
                                                         [] ( const DriveKey&                 driveKey,
                                                              bool                            isStreaming,
                                                              const std::array<uint8_t,32>&   streamId )
    {
        std::cout << "@@@ streamId: " << sirius::drive::toString(streamId) << std::endl;
    });

    for( int i=0; i<50; i++ )
    {
        std::cout << "@@@ i: " << i << " sec" << std::endl;
        sleep(1);
    }

//    gViewerSession->startWatchingLiveStream( streamTx,
//                                            gStreamerSession->publicKey(),
//                                            DRIVE_PUB_KEY,
//                                            downloadChannelHash1,
//                                            replicatorList,
//                                            VIEWER_STREAM_ROOT_FOLDER,
//                                            "testStreamFolder",
//                                            startPlayer,
//                                            {"localhost","5151"},
//                                            progress );

    int n;
    std::cin >> n;
    
    //
    // FinishStream
    //

//    gStreamerSession->finishStream( [](const sirius::drive::FinishStreamInfo& finishInfo)
//    {
//        for( auto replicator : gReplicatorArray )
//        {
//            replicator->asyncFinishStreamTxPublished( DRIVE_PUB_KEY, std::make_unique<StreamFinishRequest>(StreamFinishRequest{ streamTx, finishInfo.infoHash, finishInfo.streamSizeBytes }) );
//        }
//
//    });
    
    
    {
        std::unique_lock<std::mutex> lock(modifyCompleteMutex);
        modifyCompleteCondVar.wait( lock, [] { return modifyCompleteCounter == 4; } );
    }

    EXLOG( "@ Client started FsTree download !!!!! " );
    //sleep(2);
    //gViewerSession->setDownloadChannel( replicatorList, downloadChannelHash1 );
    //clientDownloadFsTree( gViewerSession );

    {
        std::unique_lock<std::mutex> lock(downloadStreamMutex);
        downloadStreamCondVar.wait( lock, [] { return downloadStreamEnded; } );
    }

    /// Delete client session and replicators
    //sleep(5);//(???++++!!!)
    sleep(1000);

    /// Delete client session and replicators
    gStreamerSession.reset();
    gViewerSession.reset();
    gReplicator.reset();
    gReplicator2.reset();
    gReplicator3.reset();
    gReplicator4.reset();

    _EXLOG( "@" );
    _EXLOG( "@ total time: " << float( std::clock() - startTime ) /  CLOCKS_PER_SEC );

    return 0;
}


#include "types.h"
#include "drive/ClientSession.h"
#include "drive/Replicator.h"

//#define DEBUG_NO_DAEMON_REPLICATOR_SERVICE
#include "drive/RpcReplicator.h"
#include "drive/FlatDrive.h"
#include "drive/FsTree.h"
#include "drive/Utils.h"
#include "crypto/Signer.h"
#include "utils/HexParser.h"

#include <fstream>
#include <filesystem>
#include <future>
#include <condition_variable>
#include <random>
#include "boost/date_time/posix_time/posix_time.hpp"

#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/kademlia/ed25519.hpp>

#include <sirius_drive/session_delegate.h>

bool gBreak_On_Warning = false;

const size_t REPLICATOR_NUMBER = 200;

#define ROOT_TEST_FOLDER                fs::path(getenv("HOME")) / "111-kadmlia"
#define REPLICATOR_ROOT_FOLDER          fs::path(getenv("HOME")) / "111-kadmlia" / "replicator_root"
#define REPLICATOR_SANDBOX_ROOT_FOLDER  fs::path(getenv("HOME")) / "111-kadmlia" / "sandbox_root"

#define REPLICATOR_IP_ADDR_TEMPLATE "192.168.10."
#define REPLICATOR_PORT_0           5000
#define CLIENT_WORK_FOLDER                fs::path(getenv("HOME")) / "111-kadmlia" / "client_work_folder"

fs::path gClientFolder;
static fs::path createClientFiles( size_t bigFileSize );
#define CLIENT_IP_ADDR          "192.168.10.0"
#define CLIENT_PORT             ":5000"

std::vector<sirius::crypto::KeyPair>        gKeyPairs;
std::vector<boost::asio::ip::udp::endpoint> gEndpoints;

auto clientKeyPair = sirius::crypto::KeyPair::FromPrivate(
        sirius::crypto::PrivateKey::FromString( "0000000000010203040501020304050102030405010203040501020304050102" ));


//#define DRIVE_PUB_KEY                   std::array<uint8_t,32>{1,0,0,0,0,5,6,7,8,9, 0,1,2,3,4,5,6,7,8,9, 0,1,2,3,4,5,6,7,8,9, 0,1}

namespace fs = std::filesystem;

using namespace sirius::drive;

inline std::mutex gExLogMutex;

#define EXLOG(expr) { \
__LOG( "+++ exlog: " << expr << std::endl << std::flush); \
}

// Replicators
//
std::vector<std::shared_ptr<Replicator>> gReplicators;
std::vector<std::thread> gReplicatorThreads;

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


std::shared_ptr<ClientSession> gClientSession;


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

    void onLibtorrentSessionError( const std::string& message ) override
    {
        _LOG_ERR( "onLibtorrentSessionError: " << message );
        exit(1);
    }

    // It will be called before 'replicator' shuts down
    virtual void willBeTerminated( Replicator& replicator ) override
    {
//        EXLOG( "Replicator will be terminated: " << replicator.dbgReplicatorName() );
    }

    virtual void downloadApprovalTransactionIsReady( Replicator& replicator, const DownloadApprovalTransactionInfo& info ) override
    {
        EXLOG( "downloadApprovalTransactionIsReady: " << replicator.dbgReplicatorName() );
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
        EXLOG( "modifyApprovalTransactionIsReady: " << replicator.dbgReplicatorName() );
    }

    // It will initiate the approving of single modify transaction
    virtual void singleModifyApprovalTransactionIsReady( Replicator& replicator, const ApprovalTransactionInfo& transactionInfo )  override
    {
        EXLOG( "singleModifyApprovalTransactionIsReady: " << replicator.dbgReplicatorName() );
    }

    void verificationTransactionIsReady( Replicator&                    replicator,
                                         const VerifyApprovalTxInfo&    transactionInfo ) override
    {
        EXLOG( "" );
        EXLOG( "@ verification_is_ready:" << replicator.dbgReplicatorName() );
    }

    // It will be called after the drive is synchronized with sandbox
    virtual void driveModificationIsCompleted( Replicator&                    replicator,
                                               const sirius::Key&             driveKey,
                                               const sirius::drive::InfoHash& modifyTransactionHash,
                                               const sirius::drive::InfoHash& rootHash ) override
    {
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

MyReplicatorEventHandler gMyReplicatorEventHandler;

///
/// Create replicators
///
std::string generatePrivateKey()
{
    std::random_device   dev;
    std::seed_seq        seed({dev(), dev(), dev(), dev()});
    std::mt19937         rng(seed);

    std::array<uint8_t,32> buffer{};

    std::generate( buffer.begin(), buffer.end(), [&]
    {
        return std::uniform_int_distribution<std::uint32_t>(0,0xff) ( rng );
    });

    return sirius::drive::toString( buffer );
}

endpoint_list bootstrapEndpoints;

#ifdef __APPLE__
#pragma mark --main()--
#endif

//
// main
//
int main(int,char**)
{
    gBreakOnWarning = gBreak_On_Warning;

    fs::remove_all( ROOT_TEST_FOLDER );

    auto startTime = std::clock();

    for ( int i=0; i<REPLICATOR_NUMBER; i++ )
    {
        using namespace sirius::crypto;
        using namespace boost::asio::ip;
        gKeyPairs.push_back( KeyPair::FromPrivate(PrivateKey::FromString( generatePrivateKey() )) );
        gEndpoints.push_back( udp::endpoint{ make_address( REPLICATOR_IP_ADDR_TEMPLATE + std::to_string(i+1)), uint16_t(REPLICATOR_PORT_0+i+1) }  );
    }


    std::vector<ReplicatorInfo> bootstraps;
    for ( int i=0; i<5; i++ )
    {
        bootstraps.emplace_back( ReplicatorInfo{ ReplicatorInfo{ gEndpoints[0], gKeyPairs[0].publicKey()  } } );
    }

    ///
    /// Create replicators
    ///
    {
        std::vector<std::thread> threads;
        for ( int i=0; i<REPLICATOR_NUMBER; i++ )
        {
            threads.emplace_back( [=] {
                auto replicator = createReplicator( gKeyPairs[i],
                                                   gEndpoints[i].address().to_string(),
                                                   gEndpoints[i].port(),
                                                   std::string( std::string(REPLICATOR_ROOT_FOLDER)+"_"+std::to_string(i+1) ),
                                                   std::string( "sandbox" ),
                                                   false,
                                                   bootstraps,
                                                   gMyReplicatorEventHandler,
                                                   "replicator1" );
            });
        }
        
        for( auto& t : threads )
        {
            t.join();
        }
    }
    
    ///
    /// Create client session
    ///
    gClientFolder  = createClientFiles(1024);
    gClientSession = createClientSession( clientKeyPair,
                                          CLIENT_IP_ADDR CLIENT_PORT,
                                          clientSessionErrorHandler,
                                          bootstrapEndpoints,
                                          false,
                                          "client0" );

    EXLOG("");

    // Create a lot of drives!


    EXLOG( "" );
    EXLOG( "total time: " << float( std::clock() - startTime ) /  CLOCKS_PER_SEC );

    return 0;
}

//
// replicator
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

    std::shared_ptr<Replicator> replicator;

    gDbgRpcChildCrash = true;

    replicator = createRpcReplicator(
            "127.0.0.1",
            port,
            keyPair,
            std::move( ipAddr ),
            std::to_string(port),
            std::move( rootFolder ),
            std::move( sandboxRootFolder ),
            bootstraps,
            useTcpSocket,
            handler,
            &handler,
            dbgReplicatorName );

    replicator->setDownloadApprovalTransactionTimerDelay(1);
    replicator->setModifyApprovalTransactionTimerDelay(1);
    replicator->setVerifyCodeTimerDelay(100);
    replicator->setVerifyApprovalTransactionTimerDelay(1);
    replicator->start();

    return replicator;
}

//static void modifyDrive( std::shared_ptr<Replicator>    replicator,
//                         const sirius::Key&             driveKey,
//                         const sirius::Key&             clientPublicKey,
//                         const InfoHash&                clientDataInfoHash,
//                         const sirius::Hash256&         transactionHash,
//                         const ReplicatorList&          replicatorList,
//                         uint64_t                       maxDataSize )
//{
//    replicator->asyncModify( DRIVE_PUB_KEY, std::make_unique<ModificationRequest>(ModificationRequest{ clientDataInfoHash, transactionHash, maxDataSize, replicatorList } ));
//}

//
// clientDownloadHandler
//
//static void clientDownloadHandler( download_status::code code,
//                                   const InfoHash& infoHash,
//                                   const std::filesystem::path /*filePath*/,
//                                   size_t /*downloaded*/,
//                                   size_t /*fileSize*/,
//                                   const std::string& /*errorText*/ )
//{
//    if ( code == download_status::download_complete )
//    {
//        EXLOG( "# Client received FsTree: " << toString(infoHash) );
//        EXLOG( "# FsTree file path: " << gClientFolder / "fsTree-folder" / FS_TREE_FILE_NAME );
//        gFsTree.deserialize( gClientFolder / "fsTree-folder" / FS_TREE_FILE_NAME );
//
//        // print FsTree
//        {
//            std::lock_guard<std::mutex> autolock( gExLogMutex );
//            gFsTree.dbgPrint();
//        }
//
//        isDownloadCompleted = true;
//        clientCondVar.notify_all();
//    }
//    else if ( code == download_status::dn_failed )
//    {
//        exit(-1);
//    }
//}

//
// clientDownloadFsTree
//
//static void clientDownloadFsTree( std::shared_ptr<ClientSession> clientSession, const sirius::Hash256& downloadChannelId )
//{
//    InfoHash rootHash = *driveRootHash;
//    driveRootHash.reset();
//
//    isDownloadCompleted = false;
//
//    LOG("");
//    EXLOG( "# Client started FsTree download: " << toString(rootHash) );
//
//    clientSession->download( DownloadContext(
//                                    DownloadContext::fs_tree,
//                                    clientDownloadHandler,
//                                    rootHash,
//                                    downloadChannelId, 0 ),
//                                    downloadChannelId,
//                                    gClientFolder / "fsTree-folder",
//                                    "",
//                            endpointList);
//
//    /// wait the end of file downloading
//    {
//        std::unique_lock<std::mutex> lock(clientMutex);
//        clientCondVar.wait( lock, [] { return isDownloadCompleted; } );
//    }
//}

//
// clientModifyDrive
//
//static void clientModifyDrive( const ActionList& actionList,
//                               const ReplicatorList& replicatorList,
//                               const sirius::Hash256& transactionHash )
//{
//    {
//        std::lock_guard<std::mutex> autolock( gExLogMutex );
//        actionList.dbgPrint();
//    }
//
//    // Create empty tmp folder for 'client modify data'
//    //
//    auto tmpFolder = fs::temp_directory_path() / "modify_drive_data";
//    fs::remove_all( tmpFolder );
//    fs::create_directories( tmpFolder );
//    EXLOG( "# Client tmpFolder: " << tmpFolder );
//
//    // start file uploading
//    uint64_t totalModifySize;
//    std::error_code ec;
//    InfoHash hash = gClientSession->addActionListToSession(  actionList, DRIVE_PUB_KEY, replicatorList, tmpFolder, totalModifySize, {}, ec );
//    if (ec) {
//        // handle error here
//    }
//
//    // inform replicator
//    clientModifyHash = hash;
//
//    EXLOG( "# Client is waiting the end of replicator update" );
//}

//
// clientDownloadFilesHandler
//
//int downloadFileCount;
//int downloadedFileCount;
//static void clientDownloadFilesHandler( download_status::code code,
//                                        const InfoHash& infoHash,
//                                        const std::filesystem::path filePath,
//                                        size_t downloaded,
//                                        size_t fileSize,
//                                        const std::string& errorText )
//{
//    if ( code == download_status::download_complete )
//    {
//        EXLOG( "@ download_completed: " << infoHash );
////        LOG( "@ renameAs: " << context.m_renameAs );
////        LOG( "@ saveFolder: " << context.m_saveFolder );
//        if ( ++downloadedFileCount == downloadFileCount )
//        {
//            EXLOG( "# Downloaded " << filePath << " files" );
//            isDownloadCompleted = true;
//            clientCondVar.notify_all();
//        }
//    }
//    else if ( code == download_status::downloading )
//    {
//        //LOG( "downloading: " << downloaded << " of " << fileSize );
//    }
//    else if ( code == download_status::dn_failed )
//    {
//        EXLOG( "# Error in clientDownloadFilesHandler: " << errorText );
//        exit(-1);
//    }
//}

//
// Client: read files
//
//static void clientDownloadFilesR( std::shared_ptr<ClientSession> clientSession, const Folder& folder, const sirius::Hash256& downloadChannelId, int odd )
//{
//    int counter = 0;
//    for( const auto& [name, child]: folder.childs() )
//    {
//        if ( counter++%2 == odd )
//            continue;
//
//        if ( isFolder(child) )
//        {
//            clientDownloadFilesR( clientSession, getFolder(child), downloadChannelId, odd );
//        }
//        else
//        {
//            const File& file = getFile(child);
//            std::string folderName = "root";
//            if ( folder.name() != "/" )
//                folderName = folder.name();
//            EXLOG( "# Client started download file " << hashToFileName( file.hash() ) );
//            EXLOG( "#  to " << gClientFolder / "downloaded_files" / folderName  / file.name() );
//            clientSession->download( DownloadContext(
//                    DownloadContext::file_from_drive,
//                    clientDownloadFilesHandler,
//                    file.hash(),
//                    {}, 0, true,
//                    gClientFolder / "downloaded_files" / folderName / file.name()
//                ),
//                downloadChannelId,
//                gClientFolder / "downloaded_files", "", endpointList );
//        }
//    }
//}
//static void clientDownloadFiles( std::shared_ptr<ClientSession> clientSession, int fileNumber, Folder& fsTree, const sirius::Hash256& downloadChannelId, int odd )
//{
//    isDownloadCompleted = false;
//
//    downloadFileCount = 0;
//    downloadedFileCount = 0;
//    fsTree.iterate([](const File& /*file*/) {
//        downloadFileCount++;
//        return false;
//    });
//
//    if ( downloadFileCount == 0 )
//    {
//        EXLOG( "downloadFileCount == 0" );
//        return;
//    }
//
//    downloadFileCount = fileNumber;
////    if ( fileNumber != downloadFileCount )
////    {
////        EXLOG( "!ERROR! clientDownloadFiles(): fileNumber != downloadFileCount; " << fileNumber <<"!=" << downloadFileCount );
////        exit(-1);
////    }
//
//    EXLOG("@ ======================client start downloading=== " << downloadFileCount );
//
//    clientDownloadFilesR( clientSession, fsTree, downloadChannelId, odd );
//
//    /// wait the end of file downloading
//    {
//        std::unique_lock<std::mutex> lock(clientMutex);
//        clientCondVar.wait( lock, [] { return isDownloadCompleted; } );
//    }
//}


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
        std::vector<uint8_t> data(bigFileSize);
        //std::generate( data.begin(), data.end(), std::rand );
        uint8_t counter=0;
        std::generate( data.begin(), data.end(), [&] { return counter++;} );
        std::ofstream file( b_bin );
        file.write( (char*) data.data(), data.size() );
    }
    {
        fs::path b_bin = dataFolder / "bb.bin";
        fs::create_directories( b_bin.parent_path() );
        std::vector<uint8_t> data(bigFileSize);
        //std::generate( data.begin(), data.end(), std::rand );
        uint8_t counter=11;
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

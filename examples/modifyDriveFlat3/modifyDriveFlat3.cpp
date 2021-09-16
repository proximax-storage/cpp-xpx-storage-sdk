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

#define BIG_FILE_SIZE 10 * 1024*1024
#define TRANSPORT_PROTOCOL true // true - TCP, false - uTP

// !!!
// CLIENT_IP_ADDR should be changed to proper address according to your network settings (see ifconfig)

#define CLIENT_IP_ADDR          "192.168.1.102"
#define CLIENT_PORT             5000

#define REPLICATOR_IP_ADDR      "127.0.0.1"
#define REPLICATOR_PORT         5001
#define REPLICATOR_IP_ADDR_2    "10.0.3.112"
#define REPLICATOR_PORT_2       5002
#define REPLICATOR_IP_ADDR_3    "10.0.3.113"
#define REPLICATOR_PORT_3       5003

#define ROOT_TEST_FOLDER                fs::path(getenv("HOME")) / "111"
#define REPLICATOR_ROOT_FOLDER          fs::path(getenv("HOME")) / "111" / "replicator_root"
#define REPLICATOR_SANDBOX_ROOT_FOLDER  fs::path(getenv("HOME")) / "111" / "sandbox_root"

#define REPLICATOR_ROOT_FOLDER_2          fs::path(getenv("HOME")) / "111" / "replicator_root_2"
#define REPLICATOR_SANDBOX_ROOT_FOLDER_2  fs::path(getenv("HOME")) / "111" / "sandbox_root_2"

#define REPLICATOR_ROOT_FOLDER_3          fs::path(getenv("HOME")) / "111" / "replicator_root_3"
#define REPLICATOR_SANDBOX_ROOT_FOLDER_3  fs::path(getenv("HOME")) / "111" / "sandbox_root_3"

#define CLIENT_WORK_FOLDER              fs::path(getenv("HOME")) / "111" / "client_work_folder"

#define CLIENT_PRIVATE_KEY          "0000000000010203040501020304050102030405010203040501020304050102"
#define REPLICATOR_PRIVATE_KEY      "1000000000010203040501020304050102030405010203040501020304050102"
#define REPLICATOR_PRIVATE_KEY_2    "2000000000010203040501020304050102030405010203040501020304050102"
#define REPLICATOR_PRIVATE_KEY_3    "3000000000010203040501020304050102030405010203040501020304050102"

#define DRIVE_PUB_KEY                   std::array<uint8_t,32>{1,0,0,0,0,5,6,7,8,9, 0,1,2,3,4,5,6,7,8,9, 0,1,2,3,4,5,6,7,8,9, 0,1}

const sirius::Key clientPublicKey;

const sirius::Hash256 downloadChannelHash1 = std::array<uint8_t,32>{1,1,1,1};
const sirius::Hash256 downloadChannelHash2 = std::array<uint8_t,32>{2,2,2,2};
const sirius::Hash256 downloadChannelHash3 = std::array<uint8_t,32>{3,3,3,3};

const sirius::Hash256 modifyTransactionHash1 = std::array<uint8_t,32>{0xa1,0xf,0xf,0xf};
const sirius::Hash256 modifyTransactionHash2 = std::array<uint8_t,32>{0xa2,0xf,0xf,0xf};

namespace fs = std::filesystem;

using namespace sirius::drive;

inline std::mutex gExLogMutex;

static std::string now_str();

#define EXLOG(expr) { \
        const std::lock_guard<std::mutex> autolock( gExLogMutex ); \
        std::cout << now_str() << ": " << expr << std::endl << std::flush; \
    }

// Replicators
//
std::shared_ptr<Replicator> gReplicator;
std::thread gReplicatorThread;
std::shared_ptr<Replicator> gReplicator2;
std::thread gReplicatorThread2;
std::shared_ptr<Replicator> gReplicator3;
std::thread gReplicatorThread3;

static std::shared_ptr<Replicator> createReplicator(
                                        const std::string&  pivateKey,
                                        std::string&&       ipAddr,
                                        int                 port,
                                        std::string&&       rootFolder,
                                        std::string&&       sandboxRootFolder,
                                        bool                useTcpSocket,
                                        const char*         dbgReplicatorName
                                    );

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

std::condition_variable     approveCondVar;
std::atomic<int>            approveTransactionCounter{0};

class MyReplicatorEventHandler : public ReplicatorEventHandler
{
public:

    // It will be called before 'replicator' shuts down
    virtual void willBeTerminated( Replicator& replicator ) override
    {
        EXLOG( "Replicator will be terminated: " << replicator.dbgReplicatorName() );
    }

    // It will be called when rootHash is calculated in sandbox
    virtual void rootHashIsCalculated( Replicator&                    replicator,
                                       const sirius::Key&             driveKey,
                                       const sirius::drive::InfoHash& modifyTransactionHash,
                                       const sirius::drive::InfoHash& sandboxRootHash )  override
    {
        EXLOG( "rootHshIsCalculated: " << replicator.dbgReplicatorName() );
    }
    
    // It will be called when transaction could not be completed
    virtual void modifyTransactionIsCanceled( Replicator& replicator,
                                             const sirius::Key&             driveKey,
                                             const sirius::drive::InfoHash& modifyTransactionHash,
                                             const std::string&             reason,
                                             int                            errorCode )  override
    {
        EXLOG( "modifyTransactionIsCanceled: " << replicator.dbgReplicatorName() );
    }
    
    // It will initiate the approving of modify transaction
    virtual void modifyTransactionIsApproved( Replicator& replicator, ApprovalTransactionInfo&& transactionInfo )  override
    {
        EXLOG( "modifyTransactionIsCanceled: " << replicator.dbgReplicatorName() );
    }
    
    // It will initiate the approving of single modify transaction
    virtual void singleModifyTransactionIsApproved( Replicator& replicator, ApprovalTransactionInfo&& transactionInfo )  override
    {
        EXLOG( "modifyTransactionIsCanceled: " << replicator.dbgReplicatorName() );
    }

    // It will be called after the drive is syncronized with sandbox
    virtual void driveModificationIsCompleted( Replicator&                    replicator,
                                               const sirius::Key&             driveKey,
                                               const sirius::drive::InfoHash& modifyTransactionHash,
                                               const sirius::drive::InfoHash& rootHash ) override
    {
        EXLOG( "driveModificationIsCompleted: " << replicator.dbgReplicatorName() );
    }
};


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

#pragma mark --main()--
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
    boost::asio::ip::address e3 = boost::asio::ip::address::from_string(REPLICATOR_IP_ADDR_3);
    replicatorList.emplace_back( ReplicatorInfo{ {e3, REPLICATOR_PORT_3},
        sirius::crypto::KeyPair::FromPrivate( sirius::crypto::PrivateKey::FromString( REPLICATOR_PRIVATE_KEY_3)).publicKey() } );

    boost::asio::ip::address e = boost::asio::ip::address::from_string(REPLICATOR_IP_ADDR);
    replicatorList.emplace_back( ReplicatorInfo{ {e, REPLICATOR_PORT},
        sirius::crypto::KeyPair::FromPrivate( sirius::crypto::PrivateKey::FromString( REPLICATOR_PRIVATE_KEY)).publicKey() } );
    
    boost::asio::ip::address e2 = boost::asio::ip::address::from_string(REPLICATOR_IP_ADDR_2);
    replicatorList.emplace_back( ReplicatorInfo{ {e2, REPLICATOR_PORT_2},
        sirius::crypto::KeyPair::FromPrivate( sirius::crypto::PrivateKey::FromString( REPLICATOR_PRIVATE_KEY_2)).publicKey() } );

    ///
    /// Create replicators
    ///
    gReplicator = createReplicator( REPLICATOR_PRIVATE_KEY,
                                    REPLICATOR_IP_ADDR,
                                    REPLICATOR_PORT,
                                    std::string( REPLICATOR_ROOT_FOLDER ),
                                    std::string( REPLICATOR_SANDBOX_ROOT_FOLDER ),
                                    TRANSPORT_PROTOCOL,
                                    "replicator1" );

    gReplicator2 = createReplicator( REPLICATOR_PRIVATE_KEY_2,
                                    REPLICATOR_IP_ADDR_2,
                                    REPLICATOR_PORT_2,
                                    std::string( REPLICATOR_ROOT_FOLDER_2 ),
                                    std::string( REPLICATOR_SANDBOX_ROOT_FOLDER_2 ),
                                    TRANSPORT_PROTOCOL,
                                    "replicator2" );

    gReplicator3 = createReplicator( REPLICATOR_PRIVATE_KEY_3,
                                    REPLICATOR_IP_ADDR_3,
                                    REPLICATOR_PORT_3,
                                    std::string( REPLICATOR_ROOT_FOLDER_3 ),
                                    std::string( REPLICATOR_SANDBOX_ROOT_FOLDER_3 ),
                                    TRANSPORT_PROTOCOL,
                                    "replicator3" );

    ///
    /// Create client session
    ///
    auto clientKeyPair = sirius::crypto::KeyPair::FromPrivate(
                                   sirius::crypto::PrivateKey::FromString( CLIENT_PRIVATE_KEY ));
    gClientFolder  = createClientFiles(BIG_FILE_SIZE);
    gClientSession = createClientSession( std::move(clientKeyPair),
                                         CLIENT_IP_ADDR ":5550",
                                         clientSessionErrorHandler,
                                         TRANSPORT_PROTOCOL,
                                         "client" );
    _LOG("");

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

    gReplicatorThread  = std::thread( modifyDrive, gReplicator,  DRIVE_PUB_KEY, clientKeyPair.publicKey(), clientModifyHash, modifyTransactionHash1, replicatorList, BIG_FILE_SIZE+1024 );
    sleep(1);
    gReplicatorThread2 = std::thread( modifyDrive, gReplicator2, DRIVE_PUB_KEY, clientKeyPair.publicKey(), clientModifyHash, modifyTransactionHash1, replicatorList, BIG_FILE_SIZE+1024 );
    gReplicatorThread3 = std::thread( modifyDrive, gReplicator3, DRIVE_PUB_KEY, clientKeyPair.publicKey(), clientModifyHash, modifyTransactionHash1, replicatorList, BIG_FILE_SIZE+1024 );
    
    {
        std::unique_lock<std::mutex> lock(clientMutex);
        approveCondVar.wait( lock, [] { return approveTransactionCounter == 3; } );
        
        gReplicator->acceptModifyApprovalTranaction( DRIVE_PUB_KEY, modifyTransactionHash1 );
        gReplicator2->acceptModifyApprovalTranaction( DRIVE_PUB_KEY, modifyTransactionHash1 );
        gReplicator3->acceptModifyApprovalTranaction( DRIVE_PUB_KEY, modifyTransactionHash1 );
    }

    gReplicatorThread.join();
    gReplicatorThread2.join();
    gReplicatorThread3.join();
    
    /// Print traffic distribution (for modify drive operation)
    gReplicator3->printTrafficDistribution( modifyTransactionHash1.array() );
    gReplicator->printTrafficDistribution( modifyTransactionHash1.array() );
    gReplicator2->printTrafficDistribution( modifyTransactionHash1.array() );

    /// Client: read changed fsTree (2)
    ///
    gClientSession->setDownloadChannel( replicatorList, downloadChannelHash2 );
    clientDownloadFsTree();

    /// Client: read files from drive
    clientDownloadFiles( 5, gFsTree );

    /// Client: modify drive (2)
    EXLOG( "\n# Client started: 2-st upload" );
    {
        ActionList actionList;
        //actionList.push_back( Action::move( "fff1/", "fff1/ffff1" ) );
        actionList.push_back( Action::remove( "fff1/" ) );
        actionList.push_back( Action::remove( "fff2/" ) );

        actionList.push_back( Action::remove( "a2.txt" ) );
        actionList.push_back( Action::remove( "f1/b2.bin" ) );
//        actionList.push_back( Action::remove( "f1" ) );
        actionList.push_back( Action::remove( "f2/b2.bin" ) );
        actionList.push_back( Action::move( "f2/", "f2_renamed/" ) );
        actionList.push_back( Action::move( "f2_renamed/a.txt", "f2_renamed/a_renamed.txt" ) );
        clientModifyDrive( actionList, replicatorList, modifyTransactionHash2 );
    }

    approveTransactionCounter = 0;
    
    gReplicatorThread  = std::thread( modifyDrive, gReplicator,  DRIVE_PUB_KEY, clientKeyPair.publicKey(), clientModifyHash, modifyTransactionHash2, replicatorList, BIG_FILE_SIZE+1024 );
    gReplicatorThread2 = std::thread( modifyDrive, gReplicator2, DRIVE_PUB_KEY, clientKeyPair.publicKey(), clientModifyHash, modifyTransactionHash2, replicatorList, BIG_FILE_SIZE+1024 );
    gReplicatorThread3 = std::thread( modifyDrive, gReplicator3, DRIVE_PUB_KEY, clientKeyPair.publicKey(), clientModifyHash, modifyTransactionHash2, replicatorList, BIG_FILE_SIZE+1024 );

    {
        std::unique_lock<std::mutex> lock(clientMutex);
        approveCondVar.wait( lock, [] { return approveTransactionCounter == 3; } );
        
        gReplicator->acceptModifyApprovalTranaction( DRIVE_PUB_KEY, modifyTransactionHash2 );
        gReplicator2->acceptModifyApprovalTranaction( DRIVE_PUB_KEY, modifyTransactionHash2 );
        gReplicator3->acceptModifyApprovalTranaction( DRIVE_PUB_KEY, modifyTransactionHash2 );
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

    _LOG( "\ntotal time: " << float( std::clock() - startTime ) /  CLOCKS_PER_SEC );

    return 0;
}

//
// replicator
//
#pragma mark --replicator--
static std::shared_ptr<Replicator> createReplicator(
                                        const std::string&  privateKey,
                                        std::string&&       ipAddr,
                                        int                 port,
                                        std::string&&       rootFolder,
                                        std::string&&       sandboxRootFolder,
                                        bool                useTcpSocket,
                                        const char*         dbgReplicatorName )
{
    auto clientKeyPair = sirius::crypto::KeyPair::FromPrivate(
                                   sirius::crypto::PrivateKey::FromString( privateKey ));
    
    EXLOG( "creating: " << dbgReplicatorName << " with key: " <<  int(clientKeyPair.publicKey().array()[0]) );

    auto replicator = createDefaultReplicator(
                                              std::move( clientKeyPair ),
                                              std::move( ipAddr ),
                                              std::to_string(port),
                                              std::move( rootFolder ),
                                              std::move( sandboxRootFolder ),
                                              useTcpSocket,
                                              dbgReplicatorName );

    replicator->start();
    replicator->addDrive( DRIVE_PUB_KEY, 100*1024*1024 );

    replicator->addDownloadChannelInfo( downloadChannelHash1.array(), 1024*1024,    replicatorList, { clientPublicKey } );
    replicator->addDownloadChannelInfo( downloadChannelHash2.array(), 10*1024*1024, replicatorList, { clientPublicKey } );
    replicator->addDownloadChannelInfo( downloadChannelHash3.array(), 1024*1024,    replicatorList, { clientPublicKey } );

    // set root drive hash
    driveRootHash = std::make_shared<InfoHash>( replicator->getRootHash( DRIVE_PUB_KEY ) );

    return replicator;
}

static void modifyDrive( std::shared_ptr<Replicator>    replicator,
                         const sirius::Key&             driveKey,
                         const sirius::Key&             clientPublicKey,
                         const InfoHash&                infoHash,
                         const sirius::Hash256&         transactionHash,
                         const ReplicatorList&          replicatorList,
                         uint64_t                       maxDataSize )
{
        // start drive update
    bool                    isDriveUpdated = false;
    std::condition_variable driveCondVar;
    std::mutex              driveMutex;

    replicator->modify( DRIVE_PUB_KEY, ModifyRequest{ infoHash, transactionHash, maxDataSize, replicatorList, clientPublicKey },
       [&,replicator] ( modify_status::code                            code,
                        const std::optional<ApprovalTransactionInfo>&  info,
                        const std::string&                             error )
       {
           if ( code == modify_status::update_completed )
           {
               EXLOG( "" );
               EXLOG( "@ update_completed:" << replicator->dbgReplicatorName() );

               //std::unique_lock<std::mutex> lock(driveMutex);
               isDriveUpdated = true;
               driveCondVar.notify_all();
           }
           else if ( code == modify_status::sandbox_root_hash )
           {
               EXLOG( "@ sandbox calculated: " << replicator->dbgReplicatorName() );
               EXLOG( "@   " << replicator->dbgReplicatorName() << " client: " << info->m_opinions[0].m_clientUploadBytes );
               for( uint32_t i = 0; i<info->m_opinions[0].m_replicatorsUploadBytes.size(); i++ )
               {
                   EXLOG( "@   " << replicator->dbgReplicatorName() << " " << i << ": " << info->m_opinions[0].m_replicatorsUploadBytes[i] );
               }
               approveTransactionCounter++;
               approveCondVar.notify_all();
           }
           else
           {
               EXLOG( "ERROR: " << error );
               exit(-1);
           }
       } );

    // wait the end of drive update
    {
        std::unique_lock<std::mutex> lock(driveMutex);
        driveCondVar.wait( lock, [&] { return isDriveUpdated; } );
    }

    replicator->printDriveStatus( DRIVE_PUB_KEY );

    // set root drive hash
    driveRootHash = std::make_shared<InfoHash>( replicator->getRootHash( DRIVE_PUB_KEY ) );

    EXLOG( "@ Drive modified" );
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
    if ( code == download_status::complete )
    {
        EXLOG( "# Client received FsTree: " << toString(infoHash) );
        EXLOG( "# FsTree file path: " << gClientFolder / "fsTree-folder" / FS_TREE_FILE_NAME );
        gFsTree.deserialize( gClientFolder / "fsTree-folder" / FS_TREE_FILE_NAME );

        // print FsTree
        gFsTree.dbgPrint();

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
    actionList.dbgPrint();

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
    if ( code == download_status::complete )
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
            EXLOG( "# Client started download file " << internalFileName( file.hash() ) );
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


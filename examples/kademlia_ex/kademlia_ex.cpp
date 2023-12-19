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

const size_t REPLICATOR_NUMBER = 7;


#define ROOT_TEST_FOLDER                fs::path(getenv("HOME")) / "111-kadmlia"
#define REPLICATOR_ROOT_FOLDER          fs::path(getenv("HOME")) / "111-kadmlia" / "replicator_root"
#define REPLICATOR_SANDBOX_ROOT_FOLDER  fs::path(getenv("HOME")) / "111-kadmlia" / "sandbox_root"

#ifdef __APPLE__
#define REPLICATOR_IP_ADDR_TEMPLATE "10.0.0."
#else
#define REPLICATOR_IP_ADDR_TEMPLATE "192.168.10."
#endif
#define REPLICATOR_PORT_0           5000
#define CLIENT_WORK_FOLDER                fs::path(getenv("HOME")) / "111-kadmlia" / "client_work_folder"

#ifdef __APPLE__
#define CLIENT_IP_ADDR          "10.0.0.0"
#else
#define CLIENT_IP_ADDR          "192.168.10.0"
#endif
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
//static void clientSessionErrorHandler( const lt::alert* alert )
//{
//    if ( alert->type() == lt::listen_failed_alert::alert_type )
//    {
//        std::cerr << alert->message() << std::endl << std::flush;
//        exit(-1);
//    }
//}

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
std::random_device   dev;
std::mt19937         rng(0);

std::string generatePrivateKey()
{
    //std::random_device   dev;
    //std::seed_seq        seed({dev(), dev(), dev(), dev()});
    //std::mt19937         rng(seed);

    std::array<uint8_t,32> buffer{};

    std::generate( buffer.begin(), buffer.end(), [&]
    {
        return std::uniform_int_distribution<std::uint32_t>(0,0xff) ( rng );
    });

    return sirius::drive::toString( buffer );
}

endpoint_list bootstrapEndpoints;

// Listen (socket) error handle
//
//static void clientSessionErrorHandler( const lt::alert* alert )
//{
//    if ( alert->type() == lt::listen_failed_alert::alert_type )
//    {
//        std::cerr << alert->message() << std::endl << std::flush;
//        exit(-1);
//    }
//}

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

    __attribute__((unused)) auto startTime = std::clock();

    for ( size_t i=0; i<REPLICATOR_NUMBER; i++ )
    {
        using namespace sirius::crypto;
        using namespace boost::asio::ip;
        gKeyPairs.push_back( KeyPair::FromPrivate(PrivateKey::FromString( generatePrivateKey() )) );
        gEndpoints.push_back( udp::endpoint{ make_address( REPLICATOR_IP_ADDR_TEMPLATE + std::to_string(i+1)), uint16_t(REPLICATOR_PORT_0+i+1) }  );
        ___LOG( "replicator_" << i << ": port: " << (REPLICATOR_PORT_0+i+1) << " " << gKeyPairs.back().publicKey() )
    }


    std::vector<ReplicatorInfo> bootstraps;
    for ( int i=0; i<2; i++ )
    {
        bootstraps.emplace_back( ReplicatorInfo{ ReplicatorInfo{ gEndpoints[i], gKeyPairs[i].publicKey()  } } );
    }

    ///
    /// Create replicators
    ///
    {
        //std::vector<std::thread> threads;
        for ( size_t i=0; i<REPLICATOR_NUMBER; i++ )
        {
            //threads.emplace_back( [=] {
                auto replicator = createReplicator( gKeyPairs[i],
                                                   gEndpoints[i].address().to_string(),
                                                   gEndpoints[i].port(),
                                                   std::string( std::string(REPLICATOR_ROOT_FOLDER)+"_"+std::to_string(i+1) ),
                                                   std::string( "sandbox" ),
                                                   false,
                                                   bootstraps,
                                                   gMyReplicatorEventHandler,
                                                   "replicator_" + std::to_string(i) );
                gReplicators.push_back(replicator);
                //sleep(10);
            //});
        }
        
//        for( auto& t : threads )
//        {
//            t.join();
//        }
    }
    
    ///
    /// Create client session
    ///
    //gClientFolder  = createClientFiles(1024);
//    gClientSession = createClientSession( clientKeyPair,
//                                          CLIENT_IP_ADDR CLIENT_PORT,
//                                          clientSessionErrorHandler,
//                                          bootstrapEndpoints,
//                                          false,
//                                          "client0" );

    EXLOG("");
    
    sleep(1);

    // Create a lot of drives!
    
    const size_t driveNumber = 200;
    //TODO?
    const size_t replicatorNumber = 3; // per one drive

    for( size_t i=0; i<driveNumber; i++ )
    {
        // select replicators
        std::map<size_t,std::shared_ptr<Replicator>> replicators;
        ReplicatorList replicatorList;
        while( replicators.size() < replicatorNumber )
        {
            size_t rIndex = rand() % gReplicators.size();
            if ( replicators.find(rIndex) == replicators.end() )
            {
                replicators[rIndex] = gReplicators[rIndex];
                replicatorList.push_back( gKeyPairs[rIndex].publicKey() );
            }
        }
        
        auto client = randomByteArray<sirius::Key>();

        for( auto& [index,replicator] : replicators )
        {
            auto driveRequest = std::unique_ptr<AddDriveRequest>( new AddDriveRequest{1024,0,{},replicatorList,client,{},{} } );
            
            auto driveKey = randomByteArray<sirius::Key>();
            replicator->asyncAddDrive( driveKey, std::move(driveRequest) );
            
            //___LOG( "dr_added: " << index << ";" )
        }
    }

    sleep(3);

    for(int i=0; i<2; i++)
    {
        sirius::drive::KademliaDbgInfo dbgInfo;
        std::mutex dbgInfoMutex;
        
        for( auto& replicator : gReplicators )
        {
            replicator->dbgTestKademlia( [&] (const KademliaDbgInfo& info )
                                        {
                //std::lock_guard<std::mutex> lock(dbgInfoMutex);
                dbgInfo.m_requestCounter.fetch_add( info.m_requestCounter, std::memory_order_relaxed );
                dbgInfo.m_peerCounter.fetch_add( info.m_peerCounter, std::memory_order_relaxed );
            });
        }
        sleep(2);
        ___LOG( " dbg-dbg: " << i );
    }

    for ( size_t i=0; i<REPLICATOR_NUMBER; i++ )
//    for ( size_t i=0; i<1; i++ )
    {
        ReplicatorList rList;
        ___LOG( "dbgTestKademlia2: --------------------------- i=" << i );
        gReplicators[i]->dbgTestKademlia2( rList );
        sleep(3);
    }
    
    for(;;)
    {
        sleep(1000);
    }

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
    __LOG( "creating: " << dbgReplicatorName << " with key: " <<  int(keyPair.publicKey().array()[0]) );

    std::shared_ptr<Replicator> replicator;

    gDbgRpcChildCrash = true;

    replicator = createDefaultReplicator(
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

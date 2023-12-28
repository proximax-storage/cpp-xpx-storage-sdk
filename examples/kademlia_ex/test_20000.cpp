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

bool gBreak_On_Warning = true;

const size_t NODE_NUMBER = 10; // 20000
const size_t BOOTSTRAP_NUMBER = 2; // 20



//std::vector<sirius::crypto::KeyPair>        gKeyPairs;
//std::vector<boost::asio::ip::udp::endpoint> gEndpoints;
//
//auto clientKeyPair = sirius::crypto::KeyPair::FromPrivate(
//        sirius::crypto::PrivateKey::FromString( "0000000000010203040501020304050102030405010203040501020304050102" ));


namespace fs = std::filesystem;

using namespace sirius::drive;

inline std::mutex gExLogMutex;

#define EXLOG(expr) { \
__LOG( "+++ exlog: " << expr << std::endl << std::flush); \
}


#ifdef __APPLE__
#pragma mark --main()--
#endif

using endpoint = boost::asio::ip::udp::endpoint;

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


class TestNode
{
    std::shared_ptr<kademlia::EndpointCatalogue> m_kademlia;
    std::set<kademlia::PeerKey>                  m_interestingPeerKeys;
public:
    sirius::crypto::KeyPair m_keyPair;
    endpoint                m_endpoint;

public:
    
    TestNode( endpoint theEndpoint )
    :
        m_keyPair( sirius::crypto::KeyPair::FromPrivate(sirius::crypto::PrivateKey::FromString( generatePrivateKey() )) ),
        m_endpoint(theEndpoint)
    {
    }
    
    void addInterestingPeerKey( kademlia::PeerKey& peerKey )
    {
        m_interestingPeerKeys.insert( peerKey );
        // start find ip
    }
};

std::vector<std::shared_ptr<TestNode>> gTestNodes;


class TestKademliaTransport : public kademlia::Transport
{
    boost::asio::io_context                      m_context;
    std::map<endpoint,std::shared_ptr<TestNode>> m_map;

    size_t m_messageCounter = 0;
    
public:

    TestKademliaTransport( std::vector<std::shared_ptr<TestNode>>& nodes )
    {
        for( auto& node : nodes )
        {
            m_map[node->m_endpoint] = node;
        }
    }
};


//
// main
//
int main(int,char**)
{
    gBreakOnWarning = gBreak_On_Warning;

    __attribute__((unused)) auto startTime = std::clock();


    // create nodes
    for ( size_t i=0; i<NODE_NUMBER; i++ )
    {
        endpoint theEndpoint = boost::asio::ip::udp::endpoint{ boost::asio::ip::make_address( "127.0.0.1"), uint16_t(i) };
           
        gTestNodes.push_back( std::make_shared<TestNode>(theEndpoint) );

        ___LOG( "node_" << i << ": port: " << i << " key: " << gTestNodes.back()->m_keyPair.publicKey() )
    }


    // create bootstraps
    std::vector<ReplicatorInfo> bootstraps;
    for ( int i=0; i<5; i++ )
    {
        bootstraps.emplace_back( ReplicatorInfo{ gTestNodes[i]->m_endpoint, gTestNodes[i]->m_keyPair.publicKey()  } );
    }

    EXLOG("");
    
    sleep(10);

    // Create a lot of drives!
    
    __attribute__((unused)) const size_t driveNumber = 100;
    __attribute__((unused)) const size_t replicatorNumber = 5; // per one drive

//    for( size_t i=0; i<driveNumber; i++ )
//    {
//        // select replicators
//        std::map<size_t,std::shared_ptr<Replicator>> replicators;
//        ReplicatorList replicatorList;
//        while( replicators.size() < replicatorNumber )
//        {
//            size_t rIndex = rand() % gReplicators.size();
//            if ( replicators.find(rIndex) == replicators.end() )
//            {
//                replicators[rIndex] = gReplicators[rIndex];
//                replicatorList.push_back( gKeyPairs[rIndex].publicKey() );
//            }
//        }
//
//        auto client = randomByteArray<sirius::Key>();
//
//        for( auto& [index,replicator] : replicators )
//        {
//            auto driveRequest = std::unique_ptr<AddDriveRequest>( new AddDriveRequest{1024,0,{},replicatorList,client,{},{} } );
//
//            auto driveKey = randomByteArray<sirius::Key>();
//            replicator->asyncAddDrive( driveKey, std::move(driveRequest) );
//
//            //___LOG( "dr_added: " << index << ";" )
//        }
//    }

    sleep(60);


    EXLOG( "" );
    EXLOG( "total time: " << float( std::clock() - startTime ) /  CLOCKS_PER_SEC );

    return 0;
}


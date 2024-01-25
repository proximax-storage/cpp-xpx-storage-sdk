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

const size_t NODE_NUMBER = 200*1; // 20000
const size_t BOOTSTRAP_NUMBER = 5; // 20
const size_t MAX_INTERESTING_NODES = 30;

#include "../../src/drive/Kademlia.cpp"

//std::vector<sirius::crypto::KeyPair>        gKeyPairs;
//std::vector<boost::asio::ip::udp::endpoint> gEndpoints;
//
//auto clientKeyPair = sirius::crypto::KeyPair::FromPrivate(
//        sirius::crypto::PrivateKey::FromString( "0000000000010203040501020304050102030405010203040501020304050102" ));


namespace fs = std::filesystem;

using namespace sirius::drive;
using namespace sirius::drive::kademlia;

inline std::mutex gExLogMutex;

#define EXLOG(expr) { \
__LOG( "+++ exlog: " << expr << std::endl << std::flush); \
}


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

class TestNode;

struct TestContext
{
    boost::asio::io_context                      m_context;
    std::map<endpoint,std::shared_ptr<TestNode>> m_map;
    
public:
    TestContext( std::vector<std::shared_ptr<TestNode>>& nodes );
};



class TestNode : public std::enable_shared_from_this<TestNode>, public kademlia::Transport // replicator
{
    TestContext*                                 m_testContext = nullptr;

public:
    std::shared_ptr<kademlia::EndpointCatalogue> m_kademlia;
    std::set<kademlia::PeerKey>              m_interestingPeerKeys;
    sirius::crypto::KeyPair                  m_keyPair;
    endpoint                                 m_endpoint;
    size_t m_counterMySearch = 0;
    size_t m_counterPeerSearch = 0;
public:
    
    TestNode( endpoint theEndpoint )
    :

        m_keyPair( sirius::crypto::KeyPair::FromPrivate(sirius::crypto::PrivateKey::FromString( generatePrivateKey() )) ),
        m_endpoint(theEndpoint)
    {
    // TODO remove the line below - ?
        //m_keyPair = sirius::crypto::KeyPair::FromPrivate(sirius::crypto::PrivateKey::FromString( generatePrivateKey() ));
    }

    void init(  TestContext&                        testContext,
                const std::vector<ReplicatorInfo>&  bootstraps )
    {
        m_testContext = &testContext;
        //std::__1::vector<sirius::drive::ReplicatorInfo, std::__1::allocator<sirius::drive::ReplicatorInfo>> const&, unsigned short, bool
        m_kademlia = createEndpointCatalogue( (std::weak_ptr<sirius::drive::kademlia::Transport>) shared_from_this(),
                                             (sirius::crypto::KeyPair const&) m_keyPair,
                                             (const std::vector<sirius::drive::ReplicatorInfo>&) bootstraps,
                                             (unsigned short) m_endpoint.port(),
                                             false );
    }

    void addInterestingPeerKey( kademlia::PeerKey& peerKey )
    {
        m_interestingPeerKeys.insert( peerKey );
        // start find ip
        m_kademlia->getEndpoint(peerKey); // dbgGetEndpoint = same but without search process
        //m_kademlia->dbgGetEndpointLocal(peerKey);
    }

    //
    //-----------------------------------------------------------------------------------------------------------
    //
    
    virtual void sendGetMyIpRequest( const kademlia::MyIpRequest& request, boost::asio::ip::udp::endpoint endpoint ) override
    {
        boost::asio::post( getContext(), [=,this]() //mutable
        {
            std::ostringstream os( std::ios::binary );
            cereal::PortableBinaryOutputArchive archive( os );
            archive( request );

            m_testContext->m_map[endpoint]->onGetMyIpRequest( os.str(), this->m_endpoint );
            ++m_counterMySearch;
        });
    }
    
    virtual void sendGetPeerIpRequest( const kademlia::PeerIpRequest& request, boost::asio::ip::udp::endpoint endpoint ) override
    {
        boost::asio::post( getContext(), [=,this]() //mutable
        {
            std::ostringstream os( std::ios::binary );
            cereal::PortableBinaryOutputArchive archive( os );
            archive( request );

            m_testContext->m_map[endpoint]->onGetPeerIpRequest( os.str(), this->m_endpoint );
            ++m_counterPeerSearch;
        });
    }

    // когда не спрашивали PeerInfo, а он уведомил для уменьшения трафика
    virtual void sendDirectPeerInfo( const kademlia::PeerIpResponse& response, boost::asio::ip::udp::endpoint endpoint ) override
    {
        boost::asio::post( getContext(), [=,this]() //mutable
        {
            std::ostringstream os( std::ios::binary );
            cereal::PortableBinaryOutputArchive archive( os );
            archive( response );
            m_kademlia->onGetPeerIpResponse( os.str(), endpoint );
            ++m_counterPeerSearch;
        });
    }
    
    virtual std::string onGetMyIpRequest( const std::string& requestStr, boost::asio::ip::udp::endpoint requesterEndpoint ) override
    {
        boost::asio::post( getContext(), [=,this]() //mutable
        {
            std::string searchResult = m_kademlia->onGetMyIpRequest(requestStr, requesterEndpoint);
            m_testContext->m_map[requesterEndpoint]->onGetMyIpResponse(searchResult, this->m_endpoint);
        });
        return "";
    }
    virtual std::string onGetPeerIpRequest( const std::string& requestStr, boost::asio::ip::udp::endpoint requesterEndpoint ) override
    {
        boost::asio::post( getContext(), [=,this]() //mutable
        {
            std::string searchResult = m_kademlia->onGetPeerIpRequest(requestStr, requesterEndpoint);
            m_testContext->m_map[requesterEndpoint]->onGetPeerIpResponse(searchResult, this->m_endpoint);
        });
        return "";
    }

    virtual void onGetMyIpResponse( const std::string& response, boost::asio::ip::udp::endpoint responserEndpoint ) override
    {
        boost::asio::post( getContext(), [=,this]() //mutable
        {
            m_kademlia->onGetMyIpResponse(response, responserEndpoint);
        });

    }
    virtual void onGetPeerIpResponse( const std::string& response, boost::asio::ip::udp::endpoint responserEndpoint ) override
    {
        boost::asio::post( getContext(), [=,this]() //mutable
        {
            m_kademlia->onGetPeerIpResponse(response, responserEndpoint);
        });
    }
    
    virtual boost::asio::io_context& getContext() override { return m_testContext->m_context; }
    virtual Timer     startTimer( int milliseconds, std::function<void()> func ) override
    {
        return Timer{ getContext(), milliseconds, std::move( func ) };
    }

};

std::vector<std::shared_ptr<TestNode>> gTestNodes;

TestContext::TestContext( std::vector<std::shared_ptr<TestNode>>& nodes )
{
    for( auto& node : nodes )
    {
        m_map[node->m_endpoint] = node;
    }
}

sirius::utils::ByteArray<32, sirius::Key_tag> pickRandomPeer()
{
    return gTestNodes[rand() % gTestNodes.size()]->m_keyPair.publicKey();
}

#ifdef __APPLE__
#pragma mark --main()--
#endif


//
// main
//

int c = 0;
int main(int,char**)
{
    gBreakOnWarning = false;
    
    // Skip verifications for this test
    gDoNotSkipVerification = false;

    CHECK_EXPIRED_SEC   = (15*60)*10; // 15*60
    PEER_UPDATE_SEC     = (40*60)*10;
    EXPIRED_SEC         = (2*60*60)*10;

    __attribute__((unused)) auto startTime = std::clock();

    // create nodes
    for ( size_t i = 0; i < NODE_NUMBER - 1; i++ )
    {
        endpoint theEndpoint = boost::asio::ip::udp::endpoint{ boost::asio::ip::make_address( "127.0.0.1"), uint16_t(i) };
           
        gTestNodes.push_back( std::make_shared<TestNode>(theEndpoint) );

        ___LOG( "node_" << i << ": port: " << i << " key: " << gTestNodes.back()->m_keyPair.publicKey() )
    }


    // create bootstraps
    std::vector<ReplicatorInfo> bootstraps;
    for ( size_t i = 0; i < BOOTSTRAP_NUMBER; i++ )
    {
        bootstraps.emplace_back( ReplicatorInfo{ gTestNodes[i]->m_endpoint, gTestNodes[i]->m_keyPair.publicKey()  } );
        ___LOG( "boostarap_" << i << ": port: " << bootstraps[i].m_endpoint.port() << ", address: "
            << bootstraps[i].m_endpoint.address() << ", public key: " << bootstraps[i].m_publicKey);
    }
    
    // create KademliaTransport
    TestContext testContext( gTestNodes );
    
    // init node
    for ( auto& node : gTestNodes )
    {
        node->init( testContext, bootstraps );
    }
    
    size_t interestingNodeNumber = gTestNodes.size()/3;
    if ( interestingNodeNumber > MAX_INTERESTING_NODES )
    {
        interestingNodeNumber = MAX_INTERESTING_NODES;
    }

    // test addInterestingPeerKey()
    for ( auto& node : gTestNodes )
    {
        std::vector<kademlia::PeerKey> keysToFind;
        while( keysToFind.size() < interestingNodeNumber )
        {
            auto keyToFind = pickRandomPeer();
            while (keyToFind == node->m_keyPair.publicKey()) {
                keyToFind = pickRandomPeer();
            }
            keysToFind.push_back(keyToFind);
        }

        for(auto keyToFind : keysToFind)
        {
            node->addInterestingPeerKey(keyToFind);
        }

    }

    __attribute__((unused)) auto printOne = [&](std::shared_ptr<TestNode> node)
    {
            ___LOG("FILTER (One) MyMessages " << node->m_counterMySearch
                                    << "; PeerMessages " << node->m_counterPeerSearch);
            EXLOG( "FILTER (One) !!!!!! total time: " << float( std::clock() - startTime ) /  CLOCKS_PER_SEC );
    };

    auto addLastNode = [&]()
    {
        endpoint theEndpoint = boost::asio::ip::udp::endpoint{ boost::asio::ip::make_address( "127.0.0.1"), uint16_t(NODE_NUMBER-1) };
        auto node = std::make_shared<TestNode>(theEndpoint);
        gTestNodes.push_back( node );
        testContext.m_map[node->m_endpoint] = node;
        ___LOG( "node_" << NODE_NUMBER-1 << ": port: " << NODE_NUMBER << " key: " << gTestNodes.back()->m_keyPair.publicKey() )
        node->init( testContext, bootstraps );

        std::vector<kademlia::PeerKey> keysToFind;
        while (keysToFind.size() < MAX_INTERESTING_NODES) {
            auto keyToFind = pickRandomPeer();
            while (keyToFind == node->m_keyPair.publicKey()) {
                keyToFind = pickRandomPeer();
            }
            keysToFind.push_back(keyToFind);
        }
        for(auto keyToFind : keysToFind) {
            node->addInterestingPeerKey(keyToFind);
            ___LOG("addLastNode " << keyToFind);
        }
    };

    auto printStatistic = [&]() //mutable
    {
        c++;
        double cntMyMessagesTotal = 0;
        double cntPeerMessagesTotal = 0;
        int cntFound = 0;
        int cntNotFound = 0;
        for (auto &node: gTestNodes) {
            for (auto &key: node->m_interestingPeerKeys) {
                if (node->m_kademlia->dbgGetEndpointLocal(key)) {
                    ++cntFound;
                } else {
                    ++cntNotFound;
                }
            }
            cntMyMessagesTotal += node->m_counterMySearch;
            cntPeerMessagesTotal += node->m_counterPeerSearch;

        }

            ___LOG("FILTER " << " found " << cntFound << " out of " << cntFound + cntNotFound
                             << "; MyMessages " << cntMyMessagesTotal / gTestNodes.size()
                             << "; PeerMessages " << cntPeerMessagesTotal / gTestNodes.size());
            ___LOG("iteration " << c << "--------------------------------FILTER");
            EXLOG( "FILTER total time: " << float( std::clock() - startTime ) /  CLOCKS_PER_SEC );
    };

    ___LOG("FILTER Start: " << gTestNodes.size() );

           std::thread([&]() {
        testContext.m_context.run();
        printStatistic();
        addLastNode();

        testContext.m_context.restart();
        testContext.m_context.run();
        printStatistic();
        printOne( gTestNodes.back() );
        
        ___LOG( "FILTER Ended" );
    }).detach();

    for(;;)
    {
        sleep(10);
        boost::asio::post( testContext.m_context, printStatistic );
    }

    EXLOG( "" );
    EXLOG( "total time: " << float( std::clock() - startTime ) /  CLOCKS_PER_SEC );

    return 0;
}


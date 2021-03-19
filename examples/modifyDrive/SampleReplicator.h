#include "LibTorrentSession.h"
#include "Drive.h"

#include <filesystem>
#include <iostream>
#include <fstream>

//namespace fs = std::filesystem;

//using namespace xpx_storage_sdk;

//// SampleReplicator
//class SampleReplicator
//{
//    std::shared_ptr<LibTorrentSession> m_distributor;
//    std::shared_ptr<Drive>             m_drive;
//    std::string                        m_listenInterface;

//public:
//    SampleReplicator( std::string listenInterface );

//    void restartDistributor()
//    {
//        m_distributor = createDefaultLibTorrentSession( m_listenInterface );
//    }
//};

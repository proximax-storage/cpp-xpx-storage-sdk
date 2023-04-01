/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "drive/Session.h"
#include "drive/Streaming.h"
#include "drive/ClientSession.h"
#include "drive/StreamerSession.h"
#include "drive/log.h"
#include "drive/Utils.h"
#include "crypto/Signer.h"

#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/extensions.hpp"
#include <sirius_drive/session_delegate.h>

#include <iostream>
#include <fstream>

namespace sirius::drive {

using DownloadStreamProgress = std::function<void( std::string playListPath, int chunkIndex, int chunkNumber, std::string error )>;

using StartPlayerMethod = std::function<void( std::string addr )>;

struct HttpServerParams
{
    std::string m_address;
    std::string m_port;
};

class ViewerSession : public StreamerSession
{
public:
    ViewerSession( const crypto::KeyPair& keyPair, const char* dbgOurPeerName ) : StreamerSession( keyPair, dbgOurPeerName )
    {
    }

    virtual ~ViewerSession() = default;

    virtual void startWatchingLiveStream( const Hash256&          streamId,
                                          const Key&              streamerKey,
                                          const Key&              driveKey,
                                          const Hash256&          channelId,
                                          const ReplicatorList&   replicatorSet,
                                          const fs::path&         workFolder,
                                          const endpoint_list&    replicatorEndpointList,
                                          StartPlayerMethod       startPlayerMethod,
                                          HttpServerParams        httpServerParams,
                                          DownloadStreamProgress  downloadStreamProgress ) = 0;
};

PLUGIN_API std::shared_ptr<ViewerSession> createViewerSession( const crypto::KeyPair&        keyPair,
                                                    const std::string&            address,
                                                    const LibTorrentErrorHandler& errorHandler,
                                                    const endpoint_list&          bootstraps,
                                                    bool                          useTcpSocket, // instead of uTP
                                                    const char*                   dbgClientName = "" );

}

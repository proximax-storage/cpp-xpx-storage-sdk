/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "types.h"
#include "plugins.h"
#include <boost/asio/ip/tcp.hpp>
#include <memory>

namespace sirius { namespace drive {

using  tcp = boost::asio::ip::tcp;
using  endpoint_list = std::vector<boost::asio::ip::tcp::endpoint>;

namespace modify_status {
        enum code {
            failed = 0,
            sandbox_root_hash = 1, // calculated in sandbox
            update_completed = 2,
            broken = 3 // terminated
        };
    };

    using DriveModifyHandler = std::function<void( modify_status::code, InfoHash resultRootInfoHash, const std::string& error )>;

    // Drive
    class FlatDrive {
    public:

        virtual ~FlatDrive() = default;
        //virtual void terminate() = 0;

        virtual InfoHash rootDriveHash() = 0;
        virtual void     startModifyDrive( InfoHash modifyDataInfoHash, DriveModifyHandler ) = 0;

        //todo
        virtual void     addFileIntoSession( const InfoHash& fileHash ) = 0;
        virtual void     removeFileFromSession( const InfoHash& fileHash ) = 0;

        // for testing and debugging
        virtual void printDriveStatus() = 0;
    };

    class Session;

    PLUGIN_API std::shared_ptr<FlatDrive> createDefaultFlatDrive( std::shared_ptr<Session> session,
                                                       const std::string&   replicatorRootFolder,
                                                       const std::string&   replicatorSandboxRootFolder,
                                                       const std::string&   drivePubKey,
                                                       size_t               maxSize,
                                                       const endpoint_list& otherReplicators = {}
                                                       );
}}


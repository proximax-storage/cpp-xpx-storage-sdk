/*#include <memory>
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "types.h"
#include "FsTree.h"
#include "log.h"
#include <boost/asio/ip/tcp.hpp>
#include <memory>

namespace sirius { namespace drive {

using  tcp = boost::asio::ip::tcp;
using  endpoint_list = std::vector<boost::asio::ip::tcp::endpoint>;

namespace modify_status {
        enum code {
            failed = 0,
            sandbox_root_hash = 2, // calculated in sandbox
            update_completed = 3
        };
    };

    using DriveModifyHandler = std::function<void( modify_status::code, InfoHash resultRootInfoHash, const std::string& error )>;

    // Drive
    class Drive {
    public:

        virtual ~Drive() = default;

        virtual InfoHash rootDriveHash() = 0;
        virtual void startModifyDrive( InfoHash modifyDataInfoHash, DriveModifyHandler ) = 0;

        //virtual void executeActionList( InfoHash actionListHash ) = 0;


        //todo
        //virtual bool createDriveStruct( FsTree& node, const std::string& path, const std::string& logicalPath = "" ) = 0;
    };

    std::shared_ptr<Drive> createDefaultDrive( const std::string&   listenInterface,
                                               const std::string&   replicatorRootFolder,
                                               const std::string&   replicatorSandboxRootFolder,
                                               const std::string&   drivePubKey,
                                               size_t               maxSize,
                                               const endpoint_list& otherReplicators = {}
                                               );
}}

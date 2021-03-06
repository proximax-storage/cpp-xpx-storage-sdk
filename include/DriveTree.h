/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "types.h"
#include <vector>
#include <variant>

namespace xpx_storage_sdk {
namespace drive_tree {

    // Folder
    struct Folder {
        std::string m_name;
        std::string m_logicalPath;
    };

    // File
    struct File {
        std::string m_name;
        std::string m_logicalPath;
        FileHash    m_hash;
    };

    // Node
    struct Node {

        using NodeInfo = std::variant<Folder,File>;

        Node() = default;
        Node( NodeInfo&& nodeInfo ) : m_nodeInfo(nodeInfo) {}

        std::variant<Folder,File>   m_nodeInfo;
        std::vector<Node>           m_childs;
    };
}

// DriveTree
typedef drive_tree::Node DriveTree;

}


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
namespace fs_tree {

    using Path = std::vector<std::string>;

    // File
    struct File {
        std::string m_name;
        Path        m_relativePath;
        FileHash    m_hash;
    };

    // Folder
    struct Folder {
        using Child = std::variant<Folder,File>;

        bool initWithFolder( const std::string& pathToFolder );

        std::string         m_name;
        Path                m_relativePath;
        std::vector<Child>  m_childs;
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

// FsTree
struct FsTree: public fs_tree::Node {

    FileHash serialize( std::string fileName );
    void     deserialize( std::string fileName );

    void     addFile( const std::string& destinationPath, const std::string& filename, FileHash );
    void     removeFile( const std::string& destinationPath, const std::string& filename );

    void     addFolder( const std::string& folderPath );
    void     removeFolder( const std::string& folderPath );

    void     move( const std::string& oldPathAndName, const std::string& newPathAndName );
};

}


/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "types.h"
#include <vector>
#include <set>
#include <variant>

#include <cereal/archives/binary.hpp>

namespace xpx_storage_sdk {
namespace fs_tree {

    using Path = std::vector<std::string>;

    // File
    struct File {

        template <class Archive> void serialize( Archive & arch ) {
            arch( m_name );
            arch( cereal::binary_data( m_hash.data(), m_hash.size() ) );
            //arch( m_hash );
        }

#ifdef DEBUG
        bool operator==( const File& f ) const { return m_name==f.m_name && m_hash==f.m_hash; }
#endif

        std::string m_name;
        size_t      m_size;
        Path        m_relativePath;
        FileHash    m_hash;
    };

    // Folder
    struct Folder {

        using Child = std::variant<Folder,File>;

        bool initWithFolder( const std::string& pathToFolder );

        template <class Archive> void serialize( Archive & arch ) {
            arch( m_name );
            arch( m_childs );
        }

#ifdef DEBUG
        bool operator==( const Folder& f ) const { return m_name==f.m_name && m_childs==f.m_childs; }
#endif

//        FileHash doSerialize( std::string fileName );
//        void     deserialize( std::string fileName );

        std::string         m_name;
        Path                m_relativePath;
        //std::vector<Child>  m_childs;
        std::set<Child>  m_childs;
    };

    inline bool isFolder( const Folder::Child& child ) { return child.index()==0; }

    inline bool operator<(const Folder::Child& a, const Folder::Child& b) {
        if ( isFolder(a) ) {
            if ( !isFolder(b) )
                return true;
            return std::get<0>(a).m_name < std::get<0>(b).m_name;
        }
        else {
            if ( isFolder(b) )
                return false;
            return std::get<1>(a).m_name < std::get<1>(b).m_name;
        }
        return false;
    }

}

// FsTree
struct FsTree: public fs_tree::Folder {

    FileHash doSerialize( std::string fileName );
    void     deserialize( std::string fileName );

//    void     addFile( const std::string& destinationPath, const std::string& filename, FileHash );

//    void     addFolder( const std::string& folderPath );

//    void     remove( const std::string& path );

//    void     move( const std::string& oldPathAndName, const std::string& newPathAndName );
};

}


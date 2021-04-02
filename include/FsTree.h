/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "types.h"
#include <list>
#include <variant>

#include <libtorrent/torrent_handle.hpp>
#include <cereal/archives/binary.hpp>

namespace sirius { namespace drive {

using lt_handle  = lt::torrent_handle;

// File
class File {
public:
    File() = default;

    const std::string& name() const { return m_name; }
    const InfoHash&    hash() const { return m_hash; }
    size_t             size() const { return m_size; }

    bool operator==( const File& f ) const { return m_name==f.m_name; }//TODO && m_hash==f.m_hash; }

public:
    // for cereal
    template <class Archive> void serialize( Archive & arch ) {
        arch( m_name );
        arch( cereal::binary_data( m_hash.data(), m_hash.size() ) );
        arch( m_size );
    }

private:
    friend class Folder;
    friend class FsTree;
    friend class DefaultDrive;

    File( std::string name, const InfoHash hash, size_t size ) : m_name(name), m_hash(hash), m_size(size) {}

    std::string m_name;
    InfoHash    m_hash;
    size_t      m_size;

private:
    // only for drive side
    mutable lt_handle   m_ltHandle;
};

// Folder
class Folder {
public:
    using Child = std::variant<Folder,File>;

    Folder() = default;
    Folder( std::string folderName ) : m_name(folderName) {}

    const std::string&          name()   const { return m_name; }
    const std::list<Child>&   childs() const { return m_childs; }

    bool initWithFolder( const std::string& pathToFolder );
    void dbgPrint( std::string leadingSpaces = "" ) const;

    bool operator==( const Folder& f ) const { return m_name==f.m_name && m_childs==f.m_childs; }

public:
    // for cereal
    template <class Archive> void serialize( Archive & arch ) {
        arch( m_name );
        arch( m_childs );
    }

protected:
    // creates subfolder if not exist
    Folder& getSubfolderOrCreate( const std::string& subFolderName );

    // returns nullptr if child is absent
    Child* findChild( const std::string& childName );

    // returns child iteraror
    std::list<Child>::iterator findChildIt( const std::string& childName );

    void sort();

protected:
    friend class FsTree;
    friend class DefaultDrive;

    std::string         m_name;
    std::list<Child>  m_childs;
};

// variant utilities
inline bool          isFolder( const Folder::Child& child )  { return child.index()==0; }
inline const Folder& getFolder( const Folder::Child& child ) { return std::get<0>(child); }
inline       Folder& getFolder( Folder::Child& child )       { return std::get<0>(child); }
inline const File&   getFile( const Folder::Child& child )   { return std::get<1>(child); }
inline       File&   getFile( Folder::Child& child )         { return std::get<1>(child); }

// for sorting
inline bool operator<(const Folder::Child& a, const Folder::Child& b) {
    if ( isFolder(a) ) {
        if ( !isFolder(b) )
            return true;
        return getFolder(a).name() < getFolder(b).name();
    }
    else {
        if ( isFolder(b) )
            return false;
        return getFile(a).name() < getFile(b).name();
    }
    return false;
}

// FsTree
class FsTree: public Folder {
public:

    FsTree() = default;

    void     doSerialize( std::string fileName );
    void     deserialize( std::string fileName );

    Folder*  getFolderPtr( const std::string& path, bool createIfNotExist = false );

    bool     addFile( const std::string& destinationPath, const std::string& filename, const InfoHash&, size_t size );

    bool     addFolder( const std::string& folderPath );

    bool     remove( const std::string& path );

    bool     move( const std::string& oldPathAndName, const std::string& newPathAndName, const InfoHash* newInfoHash = nullptr );

    Child*   getEntryPtr( const std::string& path );
};

}}


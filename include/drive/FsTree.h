/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "types.h"
#include "plugins.h"
#include <list>
#include <variant>
#include <functional>
#include <filesystem>

#include <libtorrent/torrent_handle.hpp>
#include <cereal/archives/binary.hpp>

namespace sirius { namespace drive {

using lt_handle  = lt::torrent_handle;

const uint32_t FS_TREE_VERSION = 0x01;

// File
class PLUGIN_API File {
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

    const lt_handle& getLtHandle() const
    {
        return m_ltHandle;
    }

private:
    friend class Folder;
    friend class FsTree;
    friend class StreamTask;

    File( std::string name, const InfoHash hash, size_t size ) : m_name(name), m_hash(hash), m_size(size) {}

    File( std::string name) : m_name(name), m_hash(InfoHash()), m_size(0) {}

    std::string m_name;
    InfoHash    m_hash;
    size_t      m_size;

private:

    // only for drive side
    mutable lt_handle   m_ltHandle;
};

// Folder
class PLUGIN_API Folder {
public:
    using Child = std::variant<Folder,File>;

    Folder() = default;
    Folder( std::string folderName ) : m_name(folderName) {}

    const std::string& name() const
    { return m_name; }

    const std::list<Child>& childs() const
    { return m_childs; }

    std::string& name()
    { return m_name; }

    bool& isaStream()
    { return m_isaStream; }

    Hash256& streamId()
    { return m_streamId; }

    bool initWithFolder( const std::string& pathToFolder );
    void dbgPrint( std::string leadingSpaces = "" ) const;

    bool operator==( const Folder& f ) const { return m_name==f.m_name && m_childs==f.m_childs; }

    bool iterate( const std::function<bool(const File&)>& func ) const;
    
    const Folder* findStreamFolder( const Hash256& streamId ) const;
    
    void getSizes( const std::filesystem::path& driveFolder,
                   const std::filesystem::path& torrentFolder,
                   uint64_t& outMetaFilesSize,
                   uint64_t& outFilesSize ) const;

public:
    // for cereal
    template <class Archive> void serialize( Archive & arch ) {
        arch( m_name );
        arch( m_childs );
        arch( m_isaStream );
        if ( m_isaStream )
        {
            arch( cereal::binary_data( m_streamId.data(), m_streamId.size() ) );
        }
    }

    // returns nullptr if child is absent
    Child* findChild( const std::string& childName );

protected:
    // creates subfolder if not exist
    Folder& getSubfolderOrCreate( const std::string& subFolderName );

    // returns child iteraror
    std::list<Child>::iterator findChildIt( const std::string& childName );

    void sort();

protected:
    friend class FsTree;
    friend class StreamTask;

    std::string       m_name;
    std::list<Child>  m_childs;
    bool              m_isaStream;
    Hash256           m_streamId;
};

// variant utilities
inline bool          isFolder( const Folder::Child& child )  { return child.index()==0; }
inline bool          isFile(   const Folder::Child& child )  { return child.index()==1; }
inline const Folder& getFolder( const Folder::Child& child ) { return std::get<0>(child); }
inline       Folder& getFolder( Folder::Child& child )       { return std::get<0>(child); }
inline const File&   getFile( const Folder::Child& child )   { return std::get<1>(child); }
inline       File&   getFile( Folder::Child& child )         { return std::get<1>(child); }

inline bool Folder::iterate( const std::function<bool(const File&)>& func ) const
{
    for( auto& child : m_childs )
    {
       if ( isFolder(child) )
       {
           if ( getFolder(child).iterate( func ) )
           {
               return true;
           }
       }
       else
       {
           const File& file = getFile(child);
           if ( func(file) )
           {
               return true;
           }
       }
    }
    return false;
}

inline const Folder* Folder::findStreamFolder( const Hash256& streamId ) const
{
    for( auto& child : m_childs )
    {
       if ( isFolder(child) )
       {
           const auto& folder = getFolder(child);
           //std::cerr << "@ findStreamFolder: " << folder.m_name << " " << folder.m_isaStream;
           if ( folder.m_isaStream )
           {
               //std::cerr << "@ ... findStreamFolder: " << folder.m_streamId << " " << streamId;
           }
           if ( folder.m_isaStream && folder.m_streamId == streamId )
           {
               return &folder;
           }
           folder.findStreamFolder( streamId );
       }
    }
    return nullptr;
}


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
class PLUGIN_API FsTree: public Folder {
public:

    FsTree() = default;

    void     doSerialize( std::string fileName );
    void     deserialize( std::string fileName );

    Folder*  getFolderPtr( const std::string& path, bool createIfNotExist = false );

    bool     addFile( const std::string& destinationPath,
                      const std::string& filename,
                      const InfoHash&    infoHash,
                      size_t             size );

    bool     addFolder( const std::string& folderPath );

    bool     remove( const std::string& path );

    bool     move( const std::string& oldPathAndName,
                   const std::string& newPathAndName,
                   const InfoHash*    newInfoHash = nullptr );

    bool     moveFlat( const std::string&     oldPathAndName,
                       const std::string&     newPathAndName,
                       std::function<void(const InfoHash&)> addInfoHashToFileMapFunc );

    bool     removeFlat( const std::string& path,
                         std::function<void(const InfoHash&)> addInfoHashToFileMapFunc );

    Child*   getEntryPtr( const std::string& path );
};

}}


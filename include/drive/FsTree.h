/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "types.h"
#include "plugins.h"
#include <map>
#include <variant>
#include <functional>
#include <filesystem>
#include "FsEntryStatistics.h"

#include <libtorrent/torrent_handle.hpp>
#include <cereal/archives/binary.hpp>

namespace sirius
{
namespace drive
{

using lt_handle = lt::torrent_handle;

const uint32_t FS_TREE_VERSION = 0x01;

// File
class PLUGIN_API File
{
public:
    File() = default;

    const std::string& name() const
    { return m_name; }

    const InfoHash& hash() const
    { return m_hash; }

    size_t size() const
    { return m_size; }

    bool isModifiable() const
    { return m_isModifiable; }

    std::shared_ptr<FileStatisticsNode> statisticsNode() const
    {
        return m_statisticsNode;
    }

    void setName( const std::string& name )
    { m_name = name; };

    void setHash( const InfoHash& hash )
    { m_hash = hash; };

    void setSize( uint64_t size )
    { m_size = size; };

    void setIsModifiable( bool isModifiable )
    { m_isModifiable = isModifiable; };

    void setStatisticsParent( std::weak_ptr<FolderStatisticsNode> node )
    {
        m_statisticsNode->setParent( std::move( node ));
    }

    void clearStatisticsNode()
    {
        m_statisticsNode = std::make_shared<FileStatisticsNode>();
    }

    bool operator==( const File& f ) const
    { return m_name == f.m_name; }//TODO && m_hash==f.m_hash; }

public:
    // for cereal
    template<class Archive>
    void serialize( Archive& arch )
    {
        arch( m_name );
        arch( cereal::binary_data( m_hash.data(), m_hash.size()));
        arch( m_size );
        arch( m_modificationIndex );
    }

private:
    friend class Folder;

    friend class FsTree;

    friend class StreamTask;

    File( std::string name, const InfoHash hash, size_t size )
            : m_name( name ), m_hash( hash ), m_size( size )
    {}

    File( std::string name,
          const InfoHash hash,
          size_t size,
          bool modifiable )
            : m_name( name ), m_hash( hash ), m_size( size ), m_isModifiable( modifiable )
    {}

    File( std::string name )
            : m_name( name ), m_hash( InfoHash()), m_size( 0 )
    {}

    std::string m_name;
    InfoHash m_hash;
    size_t m_size;
    int64_t     m_modificationIndex = -1;
    bool m_isModifiable = false;
    std::shared_ptr<FileStatisticsNode> m_statisticsNode = std::make_shared<FileStatisticsNode>();
};

// Folder
class PLUGIN_API Folder
{
public:
    using Child = std::variant<Folder, File>;

    Folder() = default;

    Folder( std::string folderName )
            : m_name( folderName )
    {}

    const std::string& name() const
    { return m_name; }

    const std::map<std::string, Child>& childs() const
    { return m_childs; }

    std::string& name()
    { return m_name; }

    bool isaStream() const
    { return m_isaStream; }

    const Hash256& streamId() const
    { return m_streamId; }

    std::shared_ptr<FolderStatisticsNode> statisticsNode() const
    {
        return m_statisticsNode;
    }

    void setStatisticsParent( std::weak_ptr<FolderStatisticsNode> node )
    {
        m_statisticsNode->setParent( std::move( node ));
    }

    void clearStatisticsNode()
    {
        m_statisticsNode = std::make_shared<FolderStatisticsNode>();
    }

    bool initWithFolder( const std::string& pathToFolder );

    void dbgPrint( std::string leadingSpaces = "" ) const;

    bool operator==( const Folder& f ) const
    { return m_name == f.m_name && m_childs == f.m_childs; }

    bool iterate( const std::function<bool( const File& )>& func ) const;

    void mapFiles( const std::function<void( File& )>& func );

    void iterateAllFolders( const std::function<void( const Folder& )>& func ) const;

    const Folder* findStreamFolder( const Hash256& streamId ) const;

    void getSizes( const std::filesystem::path& driveFolder,
                   const std::filesystem::path& torrentFolder,
                   uint64_t& outMetaFilesSize,
                   uint64_t& outFilesSize ) const;

    uint64_t evaluateSizes( const std::filesystem::path& driveFolder,
                            const std::filesystem::path& torrentFolder ) const;

    void getUniqueFiles( std::set<InfoHash>& ) const;

    void resetStatistics();

    void initializeStatistics();

public:
    // for cereal
    template<class Archive>
    void serialize( Archive& arch )
    {
        arch( m_name );
        arch( m_childs );
        arch( m_isaStream );
        if ( m_isaStream )
        {
            arch( cereal::binary_data( m_streamId.data(), m_streamId.size()));
        }
        arch( m_modificationIndex );
    }

    // returns nullptr if child is absent
    Child* findChild( const std::string& childName );

protected:
    // creates subfolder if not exist
    Folder& getSubfolderOrCreate( const std::string& subFolderName );

    // returns child iterator
    std::map<std::string, Child>::iterator findChildIt( const std::string& childName );

protected:
    friend class FsTree;

    friend class StreamTask;

    std::string m_name;
    std::map<std::string, Child> m_childs;
    bool m_isaStream = false;
    Hash256 m_streamId;
    int64_t m_modificationIndex = -1;
    std::shared_ptr<FolderStatisticsNode> m_statisticsNode = std::make_shared<FolderStatisticsNode>();
};

// variant utilities
inline bool isFolder( const Folder::Child& child )
{ return child.index() == 0; }

inline bool isFile( const Folder::Child& child )
{ return child.index() == 1; }

inline const Folder& getFolder( const Folder::Child& child )
{ return std::get<0>( child ); }

inline Folder& getFolder( Folder::Child& child )
{ return std::get<0>( child ); }

inline const File& getFile( const Folder::Child& child )
{ return std::get<1>( child ); }

inline File& getFile( Folder::Child& child )
{ return std::get<1>( child ); }

inline bool Folder::iterate( const std::function<bool( const File& )>& func ) const
{
    for ( const auto&[name, child] : m_childs )
    {
        if ( isFolder( child ))
        {
            if ( getFolder( child ).iterate( func ))
            {
                return true;
            }
        } else
        {
            const File& file = getFile( child );
            if ( func( file ))
            {
                return true;
            }
        }
    }
    return false;
}

inline void Folder::mapFiles( const std::function<void( File& )>& func )
{
    for ( auto&[name, child]: m_childs )
    {
        if ( isFolder( child ))
        {
            auto& folder = getFolder( child );
            folder.mapFiles( func );
        } else
        {
            auto& file = getFile( child );
            func( file );
        }
    }
}

inline void Folder::iterateAllFolders( const std::function<void( const Folder& )>& func ) const
{
    for ( const auto&[name, child] : m_childs )
    {
        if ( isFolder( child ))
        {
            func( getFolder( child ));
            getFolder( child ).iterateAllFolders( func );
        }
    }
}

inline const Folder* Folder::findStreamFolder( const Hash256& streamId ) const
{
    for ( const auto&[name, child] : m_childs )
    {
        if ( isFolder( child ))
        {
            const auto& folder = getFolder( child );
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
inline bool operator<( const Folder::Child& a, const Folder::Child& b )
{
    if ( isFolder( a ))
    {
        if ( !isFolder( b ))
            return true;
        return getFolder( a ).name() < getFolder( b ).name();
    } else
    {
        if ( isFolder( b ))
            return false;
        return getFile( a ).name() < getFile( b ).name();
    }
    return false;
}


// FsTree
class PLUGIN_API FsTree
        : public Folder
{
public:

    FsTree() = default;

    FsTree( const FsTree& ) = default;

    void doSerialize( std::string fileName );

    void deserialize( std::string fileName );

    Folder* getFolderPtr( const std::string& path, bool createIfNotExist = false );

    bool addFile( const std::string& destinationPath,
                  const std::string& filename,
                  const InfoHash& infoHash,
                  size_t size );

    std::optional<InfoHash> addModifiableFile( const std::string& destinationPath,
                                               const std::string& filename );

    bool addFolder( const std::string& folderPath );

//    bool     remove( const std::string& path );
//
//    bool     move( const std::string& oldPathAndName,
//                   const std::string& newPathAndName,
//                   const InfoHash*    newInfoHash = nullptr );

    bool moveFlat( const std::string& oldPathAndName,
                   const std::string& newPathAndName,
                   std::function<void( const InfoHash& )> addInfoHashToFileMapFunc );

    bool removeFlat( const std::string& path,
                     std::function<void( const InfoHash& )> addInfoHashToFileMapFunc );

    bool iterateBranch( const std::string& path,
                        std::function<bool( const Folder& )> intermediateCall,
                        std::function<bool( const Child& )> lastCall );

    Child* getEntryPtr( const std::string& path );
};

}
}


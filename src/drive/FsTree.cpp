/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "drive/FsTree.h"
#include "drive/Utils.h"
#include "drive/log.h"
#include <filesystem>
#include <iostream>
#include <fstream>

#include <cereal/types/map.hpp>
#include <cereal/types/set.hpp>
#include <cereal/types/variant.hpp>
#include <cereal/types/memory.hpp>
#include <cereal/archives/json.hpp>
#include <cereal/archives/binary.hpp>
#include <cereal/archives/portable_binary.hpp>



namespace fs = std::filesystem;

namespace sirius { namespace drive {

// dbgPring
void Folder::dbgPrint( std::string leadingSpaces ) const {

    __LOG( leadingSpaces << "• " << m_name );

    for( auto it = m_childs.begin(); it != m_childs.end(); it++ ) {

        if ( isFolder(it->second) ) {
            getFolder(it->second).dbgPrint( leadingSpaces+"  " );
        }
        else {
            __LOG( leadingSpaces << "  " << getFile(it->second).name() << " : " << getFile(it->second).size() );
        }
    }
}

// getSubfolderOrCreate
Folder& Folder::getSubfolderOrCreate( const std::string& subFolderName ) {

    auto it = m_childs.find(subFolderName);

    if ( it == m_childs.end() ) {
        m_childs[subFolderName] = Folder{subFolderName};
        return getFolder( m_childs[subFolderName] );
    }

    if ( isFile( it->second ) ) {
        throw std::runtime_error( std::string("attempt to create a folder with existing file name: ") + subFolderName );
    }

    return getFolder( it->second );
}

// findChild
Folder::Child* Folder::findChild( const std::string& childName ) {
    auto it = m_childs.find(childName);

    if ( it == m_childs.end() ) {
        return nullptr;
    }

    return &it->second;
}


// returns child iteraror
std::map<std::string, Folder::Child>::iterator Folder::findChildIt( const std::string& childName ) {
    return m_childs.find(childName);;
}

// initWithFolder
bool Folder::initWithFolder( const std::string& pathToFolder ) try {

#ifdef DEBUG
    std::srand(unsigned(std::time(nullptr)));
#endif

    m_childs.clear();
    m_name = fs::path(pathToFolder).filename().string();
    
    for (const auto& entry : std::filesystem::directory_iterator(pathToFolder)) {

        const auto entryName = entry.path().filename().string();

        if ( entry.is_directory() )
        {
            Folder subfolder{entryName};

            if ( !subfolder.initWithFolder(pathToFolder + "/" + entryName) )
                return false;

            m_childs[entryName] = subfolder;
            //m_childs.insert( subfolder );
        }
        else if ( entry.is_regular_file() )
        {
            if ( entryName != ".DS_Store" )
            {
#ifdef DEBUG
                InfoHash fileHash;
                std::generate( fileHash.begin(), fileHash.end(), std::rand );
                //m_childs.insert(  );
                m_childs.emplace_front( File{entryName,fileHash,entry.file_size()} );
#else
                m_childs[entryName] = File{entryName};
                //m_childs.insert( File{entryName} );

#endif
            }
        }
    }

    return true;
}
catch(...)
{
    return false;
}

void Folder::getSizes( const fs::path& driveFolder, const fs::path& torrentFolder, uint64_t& metaFilesSize, uint64_t& filesSize ) const
{
    for( auto it = m_childs.begin(); it != m_childs.end(); it++ )
    {
        if ( isFolder(it->second) ) {
            getFolder(it->second).getSizes( driveFolder, torrentFolder, metaFilesSize, filesSize );
        }
        else {
            const auto& file = getFile(it->second);
            _SIRIUS_ASSERT(!file.isModifiable());
            const auto& fileHash = file.hash();
            __LOG( "name:  " << getFile(it->second).name() );
            __LOG( "tname: " << torrentFolder.string() + "/" + toString(fileHash) );
            metaFilesSize += fs::file_size( torrentFolder.string() + "/" + toString(fileHash) );
            filesSize += fs::file_size( driveFolder.string() + "/" + toString(fileHash) );
        }
    }
}

uint64_t Folder::evaluateSizes( const std::filesystem::path& driveFolder,
                            const std::filesystem::path& torrentFolder) const
{
    uint64_t size = 0;
    for( auto it = m_childs.begin(); it != m_childs.end(); it++ )
    {
        if ( isFolder(it->second) ) {
            size += getFolder(it->second).evaluateSizes( driveFolder, torrentFolder);
        }
        else {
            const auto& file = getFile(it->second);
            const auto& fileHash = file.hash();
            size += fs::file_size( driveFolder.string() + "/" + toString(fileHash) );
            if (!file.isModifiable())
            {
                size += fs::file_size( torrentFolder.string() + "/" + toString(fileHash) );
            }
            else {
                // Evaluate metafiles size as 5% of
                uint64_t minMetaSize = 1024;
                size += std::max((size + 19) / 20, minMetaSize);
            }
        }
    }

    return size;
}

void Folder::getUniqueFiles( std::set<InfoHash>& files ) const
{
    for( auto it = m_childs.begin(); it != m_childs.end(); it++ )
    {
        if ( isFolder(it->second) ) {
            getFolder(it->second).getUniqueFiles( files );
        }
        else {
            const auto& fileHash = getFile(it->second).hash();
            files.insert( fileHash );
        }
    }
}

void Folder::resetStatistics()
{
    for ( auto&[name, child]: m_childs )
    {
        if ( isFile( child ))
        {
            auto& file = getFile( child );
            file.clearStatisticsNode();
        } else
        {
            auto& folder = getFolder( child );
            folder.clearStatisticsNode();
            folder.resetStatistics();
        }
    }
}

void Folder::initializeStatistics()
{
    for ( auto&[name, child]: m_childs )
    {
        if ( isFile( child ))
        {
            auto& file = getFile( child );
            file.setStatisticsParent( m_statisticsNode );
        } else
        {
            auto& folder = getFolder( child );
            folder.initializeStatistics();
            folder.setStatisticsParent( m_statisticsNode );
        }
    }
}

// doSerialize
void FsTree::doSerialize( std::string fileName ) {

    std::ofstream os( fileName, std::ios::binary );
    cereal::PortableBinaryOutputArchive archive( os );

    // save fs tree version
    archive( FS_TREE_VERSION );

    archive( *this );
}

// deserialize
void FsTree::deserialize( const std::filesystem::path& fileName ) {

    m_childs.clear();

    std::ifstream is( fileName, std::ios::binary );
    cereal::PortableBinaryInputArchive iarchive(is);

    // deserialize fs tree version
    uint32_t version;
    try {
        iarchive( version );
    }
    catch(...) {
        throw std::runtime_error( std::string("Invalid FsTree file format: ") + fileName.string() );
    }

    // check fs tree version
    if ( version != FS_TREE_VERSION ) {
        throw std::runtime_error( std::string("Invalid FS_TREE_VERSION: ") + fileName.string() );
    }

    // deserialize fs tree
    try {
        iarchive( *this );
    }
    catch(...) {
            throw std::runtime_error( std::string("Invalid FsTree file format: ") + fileName.string() );
    }
}

// addFile
bool FsTree::addFile( const std::string& destPath, const std::string& filename, const InfoHash& fileHash, size_t size ) {

    Folder* destFolder = getFolderPtr( destPath, true );

    if ( destFolder == nullptr )
        return false;

    const auto destChildIt = destFolder->findChildIt( filename );

    if ( destChildIt != destFolder->m_childs.end() )
    {
        destFolder->m_childs.erase( destChildIt );
    }

    destFolder->m_childs.emplace( filename, File{filename,fileHash,size} );

    return true;
}

// addModifiableFile
std::optional<InfoHash> FsTree::addModifiableFile( const std::string& destinationPath, const std::string& filename ) {
    Folder* destFolder = getFolderPtr( destinationPath, true );

    if ( destFolder == nullptr )
        return {};

    const auto destChildIt = destFolder->findChildIt( filename );

    if ( destChildIt != destFolder->m_childs.end() )
    {
        destFolder->m_childs.erase( destChildIt );
    }

    auto temporaryHash = randomByteArray<InfoHash>();

    File file{filename, temporaryHash,0, true};

    file.setStatisticsParent(destFolder->statisticsNode());

    destFolder->m_childs.emplace( filename, file );
    return temporaryHash;
}

// addFolder
bool FsTree::addFolder( const std::string& folderPath ) {

    Folder* folder = getFolderPtr( folderPath, true );

    if (folder != nullptr) {
        auto parentPath = fs::path(folderPath).parent_path();
        Folder* parent = getFolderPtr( parentPath.string(), false );
        if (parent != folder) {
            folder->setStatisticsParent(parent->statisticsNode());
        }
    }

    return folder != nullptr;
}

//// remove
//bool FsTree::remove( const std::string& fullPath ) {
//
//    fs::path path( fullPath );
//    std::string filename = path.filename().string();
//    Folder* parentFolder = getFolderPtr( path.parent_path().string() );
//
//    if ( parentFolder == nullptr )
//        return false;
//
//    auto it = parentFolder->findChildIt( filename );
//
//    if ( it == parentFolder->m_childs.end() )
//        return false;
//
//    parentFolder->m_childs.erase( it );
//
//    return true;
//}

// removeFlat
bool FsTree::removeFlat( const std::string& fullPath,
                         std::function<void(const InfoHash&)> addInfoHashToFileMapFunc ) {

   fs::path path( fullPath );
   std::string filename = path.filename().string();
   Folder* parentFolder = getFolderPtr( path.parent_path().string() );

   if ( parentFolder == nullptr )
       return false;

   auto it = parentFolder->findChildIt( filename );

   if ( it == parentFolder->m_childs.end() )
       return false;

   if ( isFile(it->second) ) {
       addInfoHashToFileMapFunc( getFile(it->second).m_hash );
   }

   if ( isFile(it->second) )
   {
       auto childStatistics = getFile(it->second).statisticsNode();
       childStatistics->setParent(std::weak_ptr<FolderStatisticsNode>());
   }
   else {
       auto childStatistics = getFolder(it->second).statisticsNode();
       childStatistics->setParent(std::weak_ptr<FolderStatisticsNode>());
   }

   parentFolder->m_childs.erase( it );

   return true;
}


////    Moves or renames the filesystem object identified by 'srcPath' to 'destPath' as if by the POSIX rename:
////
////    If 'srcPath' is a non-directory file, then if
////
////        - 'destPath' is the same file as 'srcPath' or a hardlink to it:
////           nothing is done in this case
////
////        - 'destPath' is existing non-directory file:
////          'destPath' is first deleted,
////          then, without allowing other processes to observe 'destPath' as deleted,
////          the pathname 'destPath' is linked to the file and 'srcPath' is unlinked from the file.
////
////        - 'destPath' is non-existing file in an existing directory:
////          The pathname 'destPath' is linked to the file and 'srcPath' is unlinked from the file.
////
////    If 'srcPath' is a directory, then if
////
////        - 'destPath' is the same directory as 'srcPath' or a hardlink to it:
////          nothing is done in this case
////
////        - 'destPath' is existing directory:
////          'destPath' is deleted if empty on POSIX systems, but this may be an error on other systems.
////          If not an error, then 'destPath' is first deleted, then, without allowing other processes to observe 'destPath' as deleted,
////          the pathname 'destPath' is linked to the directory and 'srcPath' is unlinked from the directory.
////
////        - 'destPath' is non-existing directory, not ending with a directory separator,
////          and whose parent directory exists:
////          The pathname 'destPath' is linked to the directory and 'srcPath' is unlinked from the directory.
////
////    Fails if
////        - 'destPath' ends with dot or with dot-dot
////        - 'destPath' names a non-existing directory ending with a directory separator
////        - 'srcPath' is a directory which is an ancestor of 'destPath'
////
//bool FsTree::move( const std::string& srcPathAndName, const std::string& destPathAndName, const InfoHash* newInfoHash )
//{
//    if ( fs::path( destPathAndName ) == fs::path( srcPathAndName ) )
//        return true;
//
//    fs::path srcPath( srcPathAndName );
//    std::string srcFilename = srcPath.filename().string();
//    Folder* srcParentFolder = getFolderPtr( srcPath.parent_path().string() );
//
//    if ( srcParentFolder == nullptr )
//        return false;
//
//    auto srcIt = srcParentFolder->findChildIt( srcFilename );
//
//    // src must exists
//    if ( srcIt == srcParentFolder->m_childs.end() )
//        return false;
//
//    auto destChild = srcIt->second;
//
//    fs::path destPath( destPathAndName );
//    std::string destFilename = destPath.filename().string();
//    Folder* destParentFolder = getFolderPtr( destPath.parent_path().string() );
//
//    // create destination parent folder if not exists
//    if ( destParentFolder == nullptr )
//    {
//        if ( !addFolder( destPath.parent_path().string() ) )
//            return false;
//        destParentFolder = getFolderPtr( destPath.parent_path().string(), true );
//    }
//
//    auto destIt = destParentFolder->findChildIt( destFilename );
//
//    // remove dest entry
//    if ( destIt != destParentFolder->m_childs.end() )
//    {
//        // remove it
//        destParentFolder->m_childs.erase(destIt);
//    }
//
//    // rename and set new hash for file
//    if ( isFolder(destChild) ) {
//        if ( newInfoHash != nullptr ) {
//            throw std::runtime_error("ActionList::move: newInfoHash != nullptr");
//        }
//        getFolder(destChild).m_name = destFilename;
//    }
//    else {
//        if ( newInfoHash == nullptr ) {
//            throw std::runtime_error( "ActionList::move: newInfoHash could not be nullptr" );
//        }
//        getFile(destChild).m_hash = *newInfoHash;
//        getFile(destChild).m_name = destFilename;
//    }
//
//    destParentFolder->m_childs.emplace( destFilename, destChild );
//
//    // update srcIt and remove src
//    srcParentFolder->m_childs.erase( srcIt );
//
//    return true;
//}

bool FsTree::moveFlat( const std::string& srcPathAndName,
                       const std::string& destPathAndName,
                       std::function<void(const InfoHash&)> addInfoHashToFileMapFunc )
{
   //TODO check if dest is a subfolder of src

   fs::path srcPath( srcPathAndName );
   std::string srcFilename = srcPath.filename().string();
   Folder* srcParentFolder = getFolderPtr( srcPath.parent_path().string() );

   if ( srcParentFolder == nullptr )
       return false;

   auto srcIt = srcParentFolder->findChildIt( srcFilename );

   // src must exists
   if ( srcIt == srcParentFolder->m_childs.end() )
       return false;

   if ( isFile(srcIt->second) ) {
       addInfoHashToFileMapFunc( getFile(srcIt->second).m_hash );
   }

   if ( fs::path( destPathAndName ) == fs::path( srcPathAndName ) )
       return true;

   fs::path destPath( destPathAndName );
   std::string destFilename = destPath.filename().string();
   Folder* destParentFolder = getFolderPtr( destPath.parent_path().string() );
   // create destination parent folder if not exists
   if ( destParentFolder == nullptr )
   {
       if ( !addFolder( destPath.parent_path().string() ) )
           return false;
       destParentFolder = getFolderPtr( destPath.parent_path().string(), true );
   }

    Folder::Child destChild = std::move(srcIt->second);
    if ( isFolder(destChild) )
    {
        getFolder(destChild).m_name = destFilename;
    }
    else
    {
        getFile(destChild).m_name = destFilename;
    }

   // remove dest entry if exists
   if ( auto destIt = destParentFolder->findChildIt( destFilename ); destIt != destParentFolder->m_childs.end() )
   {
       if ( isFile(destIt->second) ) {
           addInfoHashToFileMapFunc( getFile(destIt->second).m_hash );
       }

       // remove it
       destParentFolder->m_childs.erase(destIt);
   }

   if ( isFile(destChild) )
   {
       auto childStatistics = getFile(destChild).statisticsNode();
       childStatistics->setParent(destParentFolder->statisticsNode());
   }
   else {
       auto childStatistics = getFolder(destChild).statisticsNode();
       childStatistics->setParent(destParentFolder->statisticsNode());
   }

   destParentFolder->m_childs.emplace( destFilename, std::move(destChild) );

   // update srcIt and remove src
   srcParentFolder->m_childs.erase( srcIt );

   return true;
}



Folder::Child* FsTree::getEntryPtr( const std::string& pathStr )
{
    fs::path path(pathStr);

    Folder* parentFolder = this;
    if ( !path.parent_path().empty() )
    {
        parentFolder = getFolderPtr( path.parent_path().string() );
    }

    if ( parentFolder == nullptr )
        return nullptr;

    return parentFolder->findChild( path.filename().string() );
}

// getFolderPtr
Folder* FsTree::getFolderPtr( const std::string& fullPath, bool createIfNotExist )
{
    if ( fullPath.empty() || fullPath=="/" || fullPath=="\\" )
        return this;

    fs::path path( fullPath );
    Folder* treeWalker = this;

    for( auto pathIt = path.begin(); pathIt != path.end(); pathIt++ ) {

        auto it = treeWalker->m_childs.find(pathIt->string());

        if (it == treeWalker->m_childs.end() )
        {
            if ( !createIfNotExist )
                return nullptr;

            treeWalker->m_childs.emplace( pathIt->string(), Folder{pathIt->string()} );
            treeWalker = &getFolder( treeWalker->m_childs[pathIt->string()] );
        }
        else if ( isFolder(it->second) )
        {
            treeWalker = &getFolder(it->second);
        }
        else
        {
            //TODO invalid path
            return nullptr;
        }
    }

    return treeWalker;
}

bool FsTree::iterateBranch( const std::string& fullPath,
                            const std::function<bool( const Folder& )>& intermediateCall,
                            const std::function<bool( const Child& )>& lastCall )
{
    fs::path path( fullPath );

    Folder* treeWalker = this;

    if ( path.empty() ) {
        lastCall(*treeWalker);
        return true;
    }

    fs::path parentPath = path.parent_path();

    intermediateCall( *treeWalker );

    for ( const auto& pathPart : parentPath )
    {

        auto it = treeWalker->m_childs.find( pathPart.string());

        if ( it == treeWalker->m_childs.end() || !isFolder( it->second ))
        {
            return false;
        }

        treeWalker = &getFolder( it->second );
        intermediateCall( *treeWalker );
    }

    auto lastIt = treeWalker->m_childs.find( path.filename().string() );

    if ( lastIt == treeWalker->m_childs.end())
    {
        return false;
    }

    lastCall( lastIt->second );

    return true;
}

}}

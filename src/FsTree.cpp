/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "FsTree.h"
#include <filesystem>
#include <iostream>
#include <fstream>

#include <cereal/types/vector.hpp>
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

    std::cout << leadingSpaces << "• " << m_name << std::endl;

    for( auto it = m_childs.begin(); it != m_childs.end(); it++ ) {

        if ( isFolder(*it) ) {
            getFolder(*it).dbgPrint( leadingSpaces+"  " );
        }
        else {
            std::cout << leadingSpaces << "  " << getFile(*it).name() << std::endl;
        }
    }
}

// sort
void Folder::sort() {
    std::sort(m_childs.begin(), m_childs.end());

    for( auto it = m_childs.begin(); it != m_childs.end(); it++ ) {
        if ( isFolder(*it) ) {
            getFolder(*it).sort();
        }
    }
}

// getSubfolderOrCreate
Folder& Folder::getSubfolderOrCreate( const std::string& subFolderName ) {

    auto it = std::find_if( m_childs.begin(), m_childs.end(),
                         [=](const Child& child) -> bool
                         {
                             if ( isFolder(child) )
                                 return getFolder(child).name() == subFolderName;
                             return getFile(child).name() == subFolderName;
                         });

    if ( it == m_childs.end() ) {
        m_childs.emplace_back( Folder{subFolderName} );
        return getFolder( m_childs.back() );
    }

    if ( !isFolder( *it ) ) {
        throw std::runtime_error( std::string("attempt to create a folder with existing file name: ") + subFolderName );
    }

    return getFolder( *it );
}

// findChild
Folder::Child* Folder::findChild( const std::string& childName ) {
    auto it = std::find_if( m_childs.begin(), m_childs.end(),
                         [=](const Child& child) -> bool
                         {
                             if ( isFolder(child) )
                                 return getFolder(child).name() == childName;
                             return getFile(child).name() == childName;
                         });

    if ( it == m_childs.end() ) {
        return nullptr;
    }

    return &(*it);
}


// returns child iteraror
std::vector<Folder::Child>::iterator Folder::findChildIt( const std::string& childName ) {
    auto it = std::find_if(
                 m_childs.begin(),
                 m_childs.end(),
                 [=](const Child& child) -> bool
                 {
                     if ( isFolder(child) )
                         return getFolder(child).name() == childName;
                     return getFile(child).name() == childName;
                 });
    return it;
}

// initWithFolder
bool Folder::initWithFolder( const std::string& pathToFolder ) try {

#ifdef DEBUG
    std::srand(unsigned(std::time(nullptr)));
#endif

    m_childs.clear();
    m_name = fs::path(pathToFolder).filename();
    
    for (const auto& entry : std::filesystem::directory_iterator(pathToFolder)) {

        const auto entryName = entry.path().filename().string();

        if ( entry.is_directory() ) {
            //std::cout << "dir:  " << filenameStr << '\n';

            Folder subfolder{entryName};

            if ( !subfolder.initWithFolder( fs::path(pathToFolder) / entryName ) )
                return false;

            m_childs.push_back( subfolder );
            //m_childs.insert( subfolder );
        }
        else if ( entry.is_regular_file() ) {
            //std::cout << "file: " << filenameStr << '\n';


            if ( entryName != ".DS_Store" )
            {
#ifdef DEBUG
                InfoHash fileHash;
                std::generate( fileHash.begin(), fileHash.end(), std::rand );
                //m_childs.insert(  );
                m_childs.emplace_back( File{entryName,fileHash,entry.file_size()} );
#else
                m_childs.emplace_back( File{entryName} );
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

// doSerialize
void FsTree::doSerialize( std::string fileName ) {
    std::ofstream os( fileName, std::ios::binary );
    cereal::PortableBinaryOutputArchive archive( os );

    // sort tree before saving
    sort();
    archive( *this );
}

// deserialize
void FsTree::deserialize( std::string fileName ) try {
    m_childs.clear();
    std::ifstream is( fileName, std::ios::binary );
    cereal::PortableBinaryInputArchive iarchive(is);
    iarchive( *this );
}
catch(...) {
    throw std::runtime_error( std::string("Invalid FsTree file format: ") + fileName );
}

// addFile
bool FsTree::addFile( const std::string& destPath, const std::string& filename, const InfoHash& fileHash, size_t size ) {

    Folder* destFolder = getFolderPtr( destPath, true );

    if ( destFolder == nullptr )
        return false;

    auto destIt = destFolder->findChildIt( filename );

    if ( destIt != destFolder->m_childs.end() )
    {
        destFolder->m_childs.erase( destIt );
    }

    destFolder->m_childs.emplace_back( File{filename,fileHash,size} );

    return true;
}

// addFolder
bool FsTree::addFolder( const std::string& folderPath ) {

    Folder* parentFolder = getFolderPtr( folderPath, true );

    return parentFolder != nullptr;
}

// remove
bool FsTree::remove( const std::string& fullPath ) {

    fs::path path( fullPath );
    std::string filename = path.filename().string();
    Folder* parentFolder = getFolderPtr( path.parent_path().string() );
    
    if ( parentFolder == nullptr )
        return false;

    auto it = parentFolder->findChildIt( filename );

    if ( it == parentFolder->m_childs.end() )
        return false;

    parentFolder->m_childs.erase( it );

    return true;
}

//    Moves or renames the filesystem object identified by 'srcPath' to 'destPath' as if by the POSIX rename:
//
//    If 'srcPath' is a non-directory file, then if
//
//        - 'destPath' is the same file as 'srcPath' or a hardlink to it:
//           nothing is done in this case
//
//        - 'destPath' is existing non-directory file:
//          'destPath' is first deleted,
//          then, without allowing other processes to observe 'destPath' as deleted,
//          the pathname 'destPath' is linked to the file and 'srcPath' is unlinked from the file.
//
//        - 'destPath' is non-existing file in an existing directory:
//          The pathname 'destPath' is linked to the file and 'srcPath' is unlinked from the file.
//
//    If 'srcPath' is a directory, then if
//
//        - 'destPath' is the same directory as 'srcPath' or a hardlink to it:
//          nothing is done in this case
//
//        - 'destPath' is existing directory:
//          'destPath' is deleted if empty on POSIX systems, but this may be an error on other systems.
//          If not an error, then 'destPath' is first deleted, then, without allowing other processes to observe 'destPath' as deleted,
//          the pathname 'destPath' is linked to the directory and 'srcPath' is unlinked from the directory.
//
//        - 'destPath' is non-existing directory, not ending with a directory separator,
//          and whose parent directory exists:
//          The pathname 'destPath' is linked to the directory and 'srcPath' is unlinked from the directory.
//
//    Fails if
//        - 'destPath' ends with dot or with dot-dot
//        - 'destPath' names a non-existing directory ending with a directory separator
//        - 'srcPath' is a directory which is an ancestor of 'destPath'
//
bool FsTree::move( const std::string& srcPathAndName, const std::string& destPathAndName, const InfoHash* newInfoHash )
{
    if ( fs::path( destPathAndName ) == fs::path( srcPathAndName ) )
        return true;

    fs::path srcPath( srcPathAndName );
    std::string srcFilename = srcPath.filename().string();
    Folder* srcParentFolder = getFolderPtr( srcPath.parent_path().string() );

    if ( srcParentFolder == nullptr )
        return false;

    auto srcIt = srcParentFolder->findChildIt( srcFilename );

    // src must exists
    if ( srcIt == srcParentFolder->m_childs.end() )
        return false;

    fs::path destPath( destPathAndName );
    std::string destFilename = destPath.filename().string();
    Folder* destParentFolder = getFolderPtr( destPath.parent_path(), true );

    // create destination parent folder if not exists
    if ( destParentFolder == nullptr )
    {
        if ( !addFolder( destPath.parent_path() ) )
            return false;
        destParentFolder = getFolderPtr( destPath.parent_path(), true );
    }

    auto destIt = destParentFolder->findChildIt( destFilename );

    // remove dest entry
    if ( destIt != destParentFolder->m_childs.end() )
    {
        // remove it
        destParentFolder->m_childs.erase(destIt);
    }
    
    // update InfoHash (it now depends on filename)
    if ( !isFolder(*srcIt) ) {
        if ( newInfoHash == nullptr ) {
            throw std::runtime_error( "ActionList::move: newInfoHash could not be nullptr" );
        }
        getFile(*srcIt).m_hash = *newInfoHash;
    }
    else if ( isFolder(*srcIt) && newInfoHash != nullptr ) {
        return false;
    }

    destParentFolder->m_childs.emplace_back( *srcIt );
    srcParentFolder->m_childs.erase( srcIt );

    return true;
}

// getFolderPtr
Folder* FsTree::getFolderPtr( const std::string& fullPath, bool createIfNotExist )
{
//    if ( fullPath.empty() || fullPath=="/" || fullPath=="\\" )
//        return this;

    fs::path path( fullPath );
    Folder* treeValker = this;

    for( auto pathIt = path.begin(); pathIt != path.end(); pathIt++ ) {

        auto it = std::find_if( treeValker->m_childs.begin(), treeValker->m_childs.end(),
                             [=](const Child& child) -> bool
                             {
                                 return isFolder(child) && getFolder(child).name() == pathIt->string();
                             });

        if ( it == treeValker->m_childs.end() )
        {
            if ( !createIfNotExist )
                return nullptr;

            treeValker->m_childs.emplace_back( Folder{pathIt->string()} );
            treeValker = &getFolder(treeValker->m_childs.back());
        }
        else if ( isFolder(*it) )
        {
            treeValker = &getFolder(*it);
        }
        else
        {
            //TODO invalid path
            return nullptr;
        }
    }

    return treeValker;
}

}}

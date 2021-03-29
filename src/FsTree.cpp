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
bool FsTree::addFile( const std::string& destinationPath, const std::string& filename, const InfoHash& fileHash, size_t size ) {

    Folder* parentFolder = getFolderPtr( destinationPath, true );

    if ( parentFolder == nullptr )
        return false;

    parentFolder->m_childs.emplace_back( File{filename,fileHash,size} );

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

    auto it = std::find_if( parentFolder->m_childs.begin(), parentFolder->m_childs.end(),
                         [=](const Child& child) -> bool
                         {
                             if ( isFolder(child) )
                                 return getFolder(child).name() == filename;
                             return getFile(child).name() == filename;
                         });

    if ( it == parentFolder->m_childs.end() )
        return false;

    parentFolder->m_childs.erase( it );

    return true;
}

// move
bool FsTree::move( const std::string& oldPathAndName, const std::string& newPathAndName, const InfoHash* newInfoHash )
{
    if ( fs::path( newPathAndName ) == fs::path( oldPathAndName ) )
        return true;

    fs::path path( oldPathAndName );
    std::string filename = path.filename().string();
    Folder* parentFolder = getFolderPtr( path.parent_path().string() );

    auto it = std::find_if( parentFolder->m_childs.begin(), parentFolder->m_childs.end(),
                         [=](const Child& child) -> bool
                         {
                             if ( isFolder(child) )
                                 return getFolder(child).name() == filename;
                             return getFile(child).name() == filename;
                         });

    if ( it == parentFolder->m_childs.end() )
        return false;

    fs::path newPath( newPathAndName );
    std::string newFilename = newPath.filename().string();
    Folder* newParentFolder = getFolderPtr( newPath.parent_path().string(), true );

    auto newIt = std::find_if( newParentFolder->m_childs.begin(), newParentFolder->m_childs.end(),
                         [=](const Child& child) -> bool
                         {
                             if ( isFolder(child) )
                                 return getFolder(child).name() == newFilename;
                             return getFile(child).name() == newFilename;
                         });

    // newPathAndName should not exist
    if ( newIt != newParentFolder->m_childs.end() )
        return false;

    // set new InfoHash
    if ( !isFolder(*it) ) {
        if ( newInfoHash == nullptr ) {
            return false;
        }
        getFile(*it).m_hash = *newInfoHash;
    }
    else if ( isFolder(*it) && newInfoHash != nullptr ) {
        return false;
    }


    newParentFolder->m_childs.emplace_back( *it );
    parentFolder->m_childs.erase( it );

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

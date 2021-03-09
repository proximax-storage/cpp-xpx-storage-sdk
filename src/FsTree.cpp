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
#include <cereal/archives/binary.hpp>


namespace xpx_storage_sdk {
using namespace fs_tree;

bool fs_tree::Folder::initWithFolder( const std::string& pathToFolder ) try
{
#ifdef DEBUG
    std::srand(unsigned(std::time(nullptr)));
#endif

    m_childs.clear();
    m_name = pathToFolder;
    
    for (const auto& entry : std::filesystem::directory_iterator(pathToFolder)) {

        const auto entryName = entry.path().filename().string();

        if ( entry.is_directory() ) {
            //std::cout << "dir:  " << filenameStr << '\n';

            Folder subfolder{entryName};

            //TODO Windows path!
            if ( !subfolder.initWithFolder( pathToFolder+"/"+entryName ) )
                return false;

            //m_childs.push_back( subfolder );
            m_childs.insert( subfolder );
        }
        else if ( entry.is_regular_file() ) {
            //std::cout << "file: " << filenameStr << '\n';

            //m_childs.emplace_back( File{entryName} );
            if ( entryName != ".DS_Store" )
            {
#ifdef DEBUG
                FileHash fileHash;
                std::generate( fileHash.begin(), fileHash.end(), std::rand );
                m_childs.insert( File{entryName,0,std::vector<std::string>(),fileHash} );
#else
                m_childs.insert( File{entryName} );
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

//FileHash fs_tree::Folder::doSerialize( std::string fileName ) {
//    std::ofstream os( fileName, std::ios::binary );
//    cereal::BinaryOutputArchive archive( os );
//    archive( *this );

//    //TODO
//    return FileHash();
//}

//void fs_tree::Folder::deserialize( std::string fileName ) {
//    m_childs.clear();
//    std::ifstream is( fileName, std::ios::binary );
//    cereal::BinaryInputArchive iarchive(is);
//    iarchive( *this );
//}


FileHash FsTree::doSerialize( std::string fileName ) {
    std::ofstream os( fileName, std::ios::binary );
    cereal::BinaryOutputArchive archive( os );
    archive( *this );

    //TODO
    return FileHash();
}

void FsTree::deserialize( std::string fileName ) {
    m_childs.clear();
    std::ifstream is( fileName, std::ios::binary );
    cereal::BinaryInputArchive iarchive(is);
    iarchive( *this );
}

//void     addFile( const std::string& destinationPath, const std::string& filename, FileHash );

//void     addFolder( const std::string& folderPath );

//void     remove( const std::string& destinationPath, const std::string& filename );

//void     move( const std::string& oldPathAndName, const std::string& newPathAndName );


}

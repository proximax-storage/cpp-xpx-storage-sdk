/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "FsTree.h"
#include <filesystem>
#include <iostream>


namespace xpx_storage_sdk {
using namespace fs_tree;

bool fs_tree::Folder::initWithFolder( const std::string& pathToFolder ) try
{
    for (const auto& entry : std::filesystem::directory_iterator(pathToFolder)) {

        const auto entryName = entry.path().filename().string();

        if ( entry.is_directory() ) {
            //std::cout << "dir:  " << filenameStr << '\n';

            Folder subfolder{entryName};

            //TODO Windows path!
            if ( !subfolder.initWithFolder( pathToFolder+"/"+entryName ) )
                return false;

            m_childs.push_back( subfolder );
        }
        else if ( entry.is_regular_file() ) {
            //std::cout << "file: " << filenameStr << '\n';

            m_childs.emplace_back( File{entryName} );
        }
    }

    return true;
}
catch(...)
{
    return false;
}

}

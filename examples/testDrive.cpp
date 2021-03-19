/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "FsTree.h"
#include "Drive.h"
#include <iostream>

#include <algorithm>
#include <ctime>

using namespace xpx_storage_sdk;

int main() {


//    Key k;
//    KeyString kStr;
//    keyToString( k, kStr );

//    std::cout << std::string("root") + "/" + kStr + "/" << std::endl;

    FsTree fsTree;
    fsTree.initWithFolder( "/Users/alex/111" );

    fsTree.doSerialize("fsTree.bin");

    FsTree fsTree2;
    fsTree2.deserialize("fsTree.bin");

    assert( fsTree == fsTree2 );

//    fsTree.dbgPrint();

    FsTree fsTree3;
    fsTree3.addFile("", "zzz", InfoHash(), 0 );
    fsTree3.addFile("1a/2a/3a", "f123", InfoHash(), 0 );
    fsTree3.addFolder("1a/2b/3b" );
    fsTree3.addFolder("1c/2c/3c" );
    fsTree3.remove("1c/2c" );
    fsTree3.remove("1c" );
    fsTree3.dbgPrint();
    fsTree3.move("1a/2b", "1b/2b" );
    fsTree3.dbgPrint();


    return 0;
}

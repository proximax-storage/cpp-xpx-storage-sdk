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

    return 0;
}

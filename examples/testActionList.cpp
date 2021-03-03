/*
*** Copyright 2019 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "ActionList.h"

using namespace xpx_storage_sdk;

int main() {

    ActionList actList;

    actList.push_back( Action::newFolder( "/some_dir" ) );
    actList.push_back( Action::uplaod( "/home/x/file1", "/some_dir/file1" ) );
    actList.push_back( Action::newFolder( "/some_another_dir" ) );
    actList.push_back( Action::rename( "/some_dir/file1", "/some_another_dir/file1" ) );
    actList.push_back( Action::remove( "/some_dir/file1" ) );

    actList.serialize("testActList.bin");

    ActionList actList2;
    actList2.deserialize("testActList.bin");

    assert( actList == actList2 );

    return 0;
}

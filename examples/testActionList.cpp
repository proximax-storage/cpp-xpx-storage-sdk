/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "drive/ActionList.h"
#include "utils/Logging.h"

int main() {
	sirius::drive::ActionList actList;

    actList.push_back(sirius::drive::Action::newFolder("/some_dir"));
    actList.push_back(sirius::drive::Action::upload("/home/x/file1", "/some_dir/file1"));
    actList.push_back(sirius::drive::Action::newFolder("/some_another_dir"));
    actList.push_back(sirius::drive::Action::rename("/some_dir/file1", "/some_another_dir/file1"));
    actList.push_back(sirius::drive::Action::remove("/some_dir/file1"));

    actList.serialize("testActList.bin");

	sirius::drive::ActionList actList2;
    actList2.deserialize("testActList.bin");

    if (actList != actList2) {
		CATAPULT_LOG(debug) << "action list serialization failed";
		return 1;
	}

    return 0;
}

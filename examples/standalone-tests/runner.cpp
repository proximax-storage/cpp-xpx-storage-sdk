//#include "opinionReceivedRootCalculated.h"
//#include "opinionReceivedRootNotCalculated.h"
//#include "approvalReceivedRootCalculated.h"
//#include "approvalReceivedRootNotCalculated.h"
#include "slowClient.h"
#include "closeDrive.h"

#include <iostream>

//using namespace sirius::drive::test;

int main(int argc, char *argv[]) {
    if (argc != 2) {
        return 1;
    }
    int test = std::atoi(argv[1]);
    switch (test) {
//        case 1:
//            approvalReceivedRootCalculated();
//            break;
//        case 2:
//            approvalReceivedRootNotCalculated();
//            break;
//        case 3:
//            opinionReceivedRootNotCalculatedTest();
//            break;
//        case 4:
//            opinionReceivedRootCalculatedTest();
//            break;
        case 5:
            slowClient();
            break;
        case 6:
            closeDrive();
            break;
    }
}
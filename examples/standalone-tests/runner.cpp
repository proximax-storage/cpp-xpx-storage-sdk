#include "testOpinionReceivedRootCalculated.h"
#include "testOpinionReceivedRootNotCalculated.h"
#include "testApprovalReceivedRootCalculated.h"
#include "testApprovalReceivedRootNotCalculated.h"
#include "testSlowClient.h"
#include "testCloseDriveSingleModification.h"
#include "testCloseDriveNoModification.h"

#include <iostream>

//using namespace sirius::drive::test;

int main(int argc, char *argv[]) {
    if (argc != 2) {
        return 1;
    }
    int test = std::atoi(argv[1]);
    switch (test) {
        case 1:
            runApprovalReceivedRootCalculated();
            break;
        case 2:
            runApprovalReceivedRootNotCalculated();
            break;
        case 3:
            runOpinionReceivedRootNotCalculated();
            break;
        case 4:
            runOpinionReceivedRootCalculated();
            break;
        case 5:
            runSlowClientModification();
            break;
        case 6:
            runCloseDriveSingleModification();
            break;
        case 7:
            runCloseDriveNoModification();
            break;
    }

    return 0;
}
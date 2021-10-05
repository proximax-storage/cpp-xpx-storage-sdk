#include "drive/ExtensionEmulator.h"

#define RPC_ADDRESS              "192.168.0.100"
#define RPC_PORT                 "5550"

using namespace sirius::drive;

int main()
{
    ExtensionEmulator extensionEmulator(RPC_ADDRESS, RPC_PORT);
    extensionEmulator.run();

    return 0;
}


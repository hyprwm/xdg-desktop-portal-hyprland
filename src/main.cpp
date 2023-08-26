#include <sdbus-c++/sdbus-c++.h>

#include "helpers/Log.hpp"
#include "core/PortalManager.hpp"

int main(int argc, char** argv, char** envp) {
    Debug::log(LOG, "Initializing xdph...");

    g_pPortalManager = std::make_unique<CPortalManager>();
    g_pPortalManager->init();

    return 0;
}
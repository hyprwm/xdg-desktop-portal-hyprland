#include <sdbus-c++/sdbus-c++.h>

#include "helpers/Log.hpp"
#include "core/PortalManager.hpp"

void printHelp() {
    std::cout << R"#(┃ xdg-desktop-portal-hyprland
┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
┃ -v (--verbose)    → enable trace logging
┃ -q (--quiet)      → disable logging
┃ -h (--help)       → print this menu
┃ -V (--version)    → print xdph's version
)#";
}

int main(int argc, char** argv, char** envp) {
    g_pPortalManager = std::make_unique<CPortalManager>();

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--verbose" || arg == "-v")
            Debug::verbose = true;

        else if (arg == "--quiet" || arg == "-q")
            Debug::quiet = true;

        else if (arg == "--help" || arg == "-h") {
            printHelp();
            return 0;
        } else if (arg == "--version" || arg == "-V") {
            std::cout << "xdg-desktop-portal-hyprland v" << XDPH_VERSION << "\n";
            return 0;
        } else {
            printHelp();
            return 1;
        }
    }

    Debug::log(LOG, "Initializing xdph...");

    g_pPortalManager->init();

    return 0;
}

#include "Picker.hpp"

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <string>

namespace {
    void onTermSignal(int sig) {
        // minimal: flush stdout (xdph reads our selection from there) and exit cleanly.
        // we treat any signal during run as a cancel: no [SELECTION] emitted, xdph aborts.
        std::cout.flush();
        std::_Exit(128 + sig);
    }
}

int main(int argc, char** argv) {
    // our stdout is the IPC channel to xdph (the [SELECTION] line). keep hyprtoolkit's
    // debug logging off it so nothing can ever interleave with or trail the selection.
    // must be set before the backend is created (the logger reads it on first log).
    setenv("HT_QUIET", "1", 1);

    struct sigaction sa{};
    sa.sa_handler = onTermSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);

    // slurp / hyprctl write to our stdout/stderr if they go wrong; SIGPIPE on a
    // dead xdph parent would otherwise kill us mid-render.
    signal(SIGPIPE, SIG_IGN);

    bool allowToken = false;
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == std::string{"--allow-token"})
            allowToken = true;
    }

    try {
        CPicker picker(allowToken);
        return picker.run();
    } catch (const std::exception& e) {
        std::cerr << "[picker] fatal: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "[picker] fatal: unknown exception\n";
        return 1;
    }
}

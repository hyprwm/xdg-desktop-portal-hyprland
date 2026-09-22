#include "Picker.hpp"
#include "PickerData.hpp"

#include <hyprtoolkit/core/Backend.hpp>

#include <cstdlib>
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
    setenv("HT_QUIET", "1", 0);

    bool allowTokenByDefault = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view{argv[i]} == "--allow-token")
            allowTokenByDefault = true;
    }

    const auto BACKEND = Hyprtoolkit::IBackend::create();
    if (!BACKEND) {
        std::cerr << "[XDPH_PICKER_ERROR] Failed to initialize Hyprtoolkit\n";
        return 1;
    }

    BACKEND->setLogFn([](Hyprtoolkit::eLogLevel, const std::string& message) { std::cerr << "[hyprtoolkit] " << message << '\n'; });

    const auto WINDOW_LIST = std::getenv("XDPH_WINDOW_SHARING_LIST");
    const auto OUTPUT_LIST = std::getenv("XDPH_OUTPUT_SHARING_LIST");

    CPicker    picker(BACKEND, parseOutputList(OUTPUT_LIST ? OUTPUT_LIST : ""), parseWindowList(WINDOW_LIST ? WINDOW_LIST : ""), allowTokenByDefault);
    if (!picker.initialize()) {
        std::cerr << "[XDPH_PICKER_ERROR] Failed to create the picker window\n";
        BACKEND->destroy();
        return 1;
    }

    picker.run();
    return 0;
}

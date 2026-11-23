#include "PickerOptions.hpp"
#include "ui/FilePicker.hpp"

#include <hyprtoolkit/core/Backend.hpp>

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

int main(int argc, char** argv) {
    setenv("HT_QUIET", "1", 0);

    const std::vector<std::string_view> ARGS(argv + 1, argv + argc);

    auto                                options = parsePickerOptions(ARGS);
    if (!options) {
        std::cerr << "[XDPH_PICKER_ERROR] " << options.error() << '\n';
        return 1;
    }

    const auto BACKEND = Hyprtoolkit::IBackend::create();
    if (!BACKEND) {
        std::cerr << "[XDPH_PICKER_ERROR] Failed to initialize Hyprtoolkit\n";
        return 1;
    }

    BACKEND->setLogFn([](Hyprtoolkit::eLogLevel, const std::string& message) { std::cerr << "[hyprtoolkit] " << message << '\n'; });

    std::optional<SPickerResult> result;
    {
        CFilePicker picker(BACKEND, std::move(*options));
        if (!picker.initialize()) {
            std::cerr << "[XDPH_PICKER_ERROR] Failed to create the picker window\n";
            BACKEND->destroy();
            return 1;
        }

        result = picker.run();
    }

    if (result)
        std::cout << formatPickerResult(*result) << std::flush;

    return 0;
}

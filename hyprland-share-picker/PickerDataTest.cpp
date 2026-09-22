#include "PickerData.hpp"

#include <iostream>

static bool expect(bool condition, const char* message) {
    if (condition)
        return true;

    std::cerr << message << '\n';
    return false;
}

int main() {
    bool       success = true;

    const auto WINDOWS = parseWindowList("4294967295[HC>]kitty[HT>]Terminal[HE>]123[HA>]broken[HC>]");
    success &= expect(WINDOWS.size() == 1, "window parser accepted a malformed record");
    if (!WINDOWS.empty()) {
        success &= expect(WINDOWS[0].id == UINT32_MAX, "window parser lost a uint32 id");
        success &= expect(WINDOWS[0].windowClass == "kitty" && WINDOWS[0].title == "Terminal", "window parser mixed up fields");
    }

    const auto OUTPUTS = parseOutputList("4:DP-1:-1920:0:1920:1080;6:HDMI-1:0:0:2560:1440;");
    success &= expect(OUTPUTS.size() == 2, "output parser rejected valid records");
    if (!OUTPUTS.empty())
        success &= expect(OUTPUTS[0].name == "DP-1" && OUTPUTS[0].x == -1920 && OUTPUTS[0].width == 1920, "output parser produced incorrect geometry");
    success &= expect(parseOutputList("20:DP-1:0:0:1:1;").empty(), "output parser accepted a truncated name");

    const auto REGION = parseRegion("DP-1 20 20 800 600\n", OUTPUTS);
    success &= expect(REGION.has_value(), "region parser rejected a valid region");
    success &= expect(REGION && REGION->x == 20 && REGION->y == 20 && REGION->width == 800 && REGION->height == 600, "region parser converted coordinates incorrectly");
    success &= expect(!parseRegion("DP-1 20 20 -1 600", OUTPUTS), "region parser accepted a negative width");
    success &= expect(!parseRegion("unknown 0 0 10 10", OUTPUTS), "region parser accepted an unknown output");

    const SSelection WINDOW_SELECTION{
        .type     = SELECTION_WINDOW,
        .output   = "",
        .windowID = UINT32_MAX,
    };
    success &= expect(formatSelection(WINDOW_SELECTION, true) == "[SELECTION]r/window:4294967295\n", "window selection was serialized incorrectly");
    success &= expect(escapeMarkup("<&>'\"\n") == "&lt;&amp;&gt;&apos;&quot; ", "markup escaping is incomplete");

    return success ? 0 : 1;
}

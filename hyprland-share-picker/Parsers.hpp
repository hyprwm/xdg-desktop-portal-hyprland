#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Picker {

    struct SWindowEntry {
        std::string        title;
        std::string        clazz;
        unsigned long long id = 0;
    };

    struct SMonitorEntry {
        std::string name;
        long long   id     = 0;
        long long   x      = 0;
        long long   y      = 0;
        long long   width  = 0;
        long long   height = 0;
    };

    // Parse XDPH_WINDOW_SHARING_LIST: <id>[HC>]<class>[HT>]<title>[HE>]<addr>[HA>] ...
    std::vector<SWindowEntry> parseWindowList(const std::string& env);

    // Parse text output of `hyprctl monitors`. Each monitor block:
    //   Monitor <NAME> (ID <N>):
    //           <W>x<H>@<refresh> at <X>x<Y>
    //           ...
    std::vector<SMonitorEntry> parseMonitors(const std::string& hyprctlOut);

    // Convert slurp output "<name> <gx> <gy> <w> <h>" to a "<name>@<lx>,<ly>,<w>,<h>"
    // string with monitor-local coords. Returns nullopt on parse failure.
    std::optional<std::string> parseRegion(const std::string& slurpOut, const std::vector<SMonitorEntry>& monitors);

}

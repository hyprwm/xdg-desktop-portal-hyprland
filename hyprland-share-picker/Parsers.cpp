#include "Parsers.hpp"

#include <algorithm>
#include <sstream>

namespace Picker {

    std::vector<SWindowEntry> parseWindowList(const std::string& env) {
        std::vector<SWindowEntry> result;
        if (env.empty())
            return result;

        std::string rolling = env;
        while (!rolling.empty()) {
            const auto IDSEP    = rolling.find("[HC>]");
            const auto CLASSSEP = rolling.find("[HT>]");
            const auto TITLESEP = rolling.find("[HE>]");
            const auto ADDRSEP  = rolling.find("[HA>]");

            if (IDSEP == std::string::npos || CLASSSEP == std::string::npos || TITLESEP == std::string::npos || ADDRSEP == std::string::npos)
                break;
            if (!(IDSEP < CLASSSEP && CLASSSEP < TITLESEP && TITLESEP < ADDRSEP))
                break;

            const auto IDSTR    = rolling.substr(0, IDSEP);
            const auto CLASSSTR = rolling.substr(IDSEP + 5, CLASSSEP - IDSEP - 5);
            const auto TITLESTR = rolling.substr(CLASSSEP + 5, TITLESEP - CLASSSEP - 5);

            try {
                result.push_back({TITLESTR, CLASSSTR, std::stoull(IDSTR)});
            } catch (...) {
                // skip malformed entry
            }

            rolling = rolling.substr(ADDRSEP + 5);
        }

        return result;
    }

    std::vector<SMonitorEntry> parseMonitors(const std::string& hyprctlOut) {
        std::vector<SMonitorEntry> result;
        if (hyprctlOut.empty())
            return result;

        std::istringstream stream(hyprctlOut);
        std::string        line;

        while (std::getline(stream, line)) {
            if (!line.starts_with("Monitor "))
                continue;

            SMonitorEntry entry;

            const auto    NAME_BEGIN = std::string("Monitor ").size();
            const auto    NAME_END   = line.find(" (");
            const auto    ID_BEGIN   = line.find("(ID ");
            const auto    ID_END     = line.find("):");

            if (NAME_END == std::string::npos || ID_BEGIN == std::string::npos || ID_END == std::string::npos)
                continue;
            if (NAME_BEGIN >= NAME_END || ID_BEGIN + 4 >= ID_END)
                continue;

            entry.name = line.substr(NAME_BEGIN, NAME_END - NAME_BEGIN);

            try {
                entry.id = std::stoll(line.substr(ID_BEGIN + 4, ID_END - ID_BEGIN - 4));
            } catch (...) { continue; }

            // next line: "        WxH@HZ at XxY"
            if (!std::getline(stream, line))
                break;

            long long w = 0, h = 0, x = 0, y = 0;
            float     hz = 0;
            if (std::sscanf(line.c_str(), " %lldx%lld@%f at %lldx%lld", &w, &h, &hz, &x, &y) != 5)
                continue;

            entry.width  = w;
            entry.height = h;
            entry.x      = x;
            entry.y      = y;
            result.push_back(entry);
        }

        return result;
    }

    std::optional<std::string> parseRegion(const std::string& slurpOut, const std::vector<SMonitorEntry>& monitors) {
        if (slurpOut.empty())
            return std::nullopt;

        std::string clean = slurpOut;
        clean.erase(std::remove(clean.begin(), clean.end(), '\n'), clean.end());

        std::istringstream stream(clean);
        std::string        screenName;
        long long          gx = 0, gy = 0, w = 0, h = 0;
        stream >> screenName >> gx >> gy >> w >> h;
        if (screenName.empty() || stream.fail())
            return std::nullopt;

        const auto MONITOR = std::find_if(monitors.begin(), monitors.end(), [&](const auto& m) { return m.name == screenName; });
        if (MONITOR == monitors.end())
            return std::nullopt;

        const auto LX = gx - MONITOR->x;
        const auto LY = gy - MONITOR->y;

        return screenName + "@" + std::to_string(LX) + "," + std::to_string(LY) + "," + std::to_string(w) + "," + std::to_string(h);
    }

}

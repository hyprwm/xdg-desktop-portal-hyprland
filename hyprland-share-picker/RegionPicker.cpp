#include "RegionPicker.hpp"

#include <hyprutils/os/Process.hpp>

std::optional<SSelection> selectRegion(const std::vector<SOutputEntry>& outputs) {
    Hyprutils::OS::CProcess process("slurp", {"-f", "%o %X %Y %W %H"});
    if (!process.runSync() || process.exitCode() != 0)
        return std::nullopt;

    return parseRegion(process.stdOut(), outputs);
}

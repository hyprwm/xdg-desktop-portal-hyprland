#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct SPlace {
    std::string           label;
    std::filesystem::path path;
    std::string           icon;
};

// the XDG_*_DIR entries of a user-dirs.dirs file, keyed like "DOWNLOAD"
std::unordered_map<std::string, std::filesystem::path> parseUserDirs(std::string_view data, const std::filesystem::path& home);

// home, the XDG user directories that exist, and the filesystem root
std::vector<SPlace> defaultPlaces();

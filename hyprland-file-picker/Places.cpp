#include "Places.hpp"
#include "Directory.hpp"

#include <array>
#include <cstdlib>
#include <ranges>

struct SUserDir {
    const char* key;
    const char* label;
    const char* icon;
};

// in sidebar order, the labels double as the xdg-user-dirs defaults
static constexpr std::array<SUserDir, 6> USER_DIRS = {
    SUserDir{"DESKTOP", "Desktop", "user-desktop"}, SUserDir{"DOCUMENTS", "Documents", "folder-documents"}, SUserDir{"DOWNLOAD", "Downloads", "folder-download"},
    SUserDir{"MUSIC", "Music", "folder-music"},     SUserDir{"PICTURES", "Pictures", "folder-pictures"},    SUserDir{"VIDEOS", "Videos", "folder-videos"},
};

std::unordered_map<std::string, std::filesystem::path> parseUserDirs(std::string_view data, const std::filesystem::path& home) {
    std::unordered_map<std::string, std::filesystem::path> dirs;

    for (const auto RAW : data | std::views::split('\n')) {
        std::string_view line{RAW.begin(), RAW.end()};
        while (line.starts_with(' ') || line.starts_with('\t')) {
            line.remove_prefix(1);
        }

        // XDG_<KEY>_DIR="$HOME/relative" or XDG_<KEY>_DIR="/absolute"
        const auto EQUALS = line.find('=');
        if (!line.starts_with("XDG_") || EQUALS == std::string_view::npos)
            continue;

        auto key = line.substr(4, EQUALS - 4);
        if (!key.ends_with("_DIR"))
            continue;

        key.remove_suffix(4);

        auto value = line.substr(EQUALS + 1);
        if (value.size() < 2 || !value.starts_with('"') || !value.ends_with('"'))
            continue;

        value = value.substr(1, value.size() - 2);

        if (value == "$HOME")
            dirs[std::string{key}] = home;
        else if (value.starts_with("$HOME/"))
            dirs[std::string{key}] = home / value.substr(6);
        else if (value.starts_with('/'))
            dirs[std::string{key}] = value;
    }

    return dirs;
}

std::vector<SPlace> defaultPlaces() {
    std::vector<SPlace> places;

    if (const auto HOME_ENV = std::getenv("HOME"); HOME_ENV && *HOME_ENV) {
        const std::filesystem::path HOME{HOME_ENV};
        places.emplace_back(SPlace{.label = "Home", .path = HOME, .icon = "user-home"});

        std::unordered_map<std::string, std::filesystem::path> dirs;
        for (const auto& dir : USER_DIRS) {
            dirs[dir.key] = HOME / dir.label;
        }

        const auto            CONFIG_ENV = std::getenv("XDG_CONFIG_HOME");
        std::filesystem::path config     = CONFIG_ENV && *CONFIG_ENV ? std::filesystem::path{CONFIG_ENV} : HOME / ".config";
        if (const auto DATA = readTextFile(config / "user-dirs.dirs"); DATA) {
            for (auto& [key, path] : parseUserDirs(*DATA, HOME)) {
                dirs[key] = std::move(path);
            }
        }

        for (const auto& dir : USER_DIRS) {
            const auto& path = dirs[dir.key];

            // a user dir set to home means it's disabled
            std::error_code ec;
            if (path == HOME || !std::filesystem::is_directory(path, ec))
                continue;

            places.emplace_back(SPlace{.label = dir.label, .path = path, .icon = dir.icon});
        }
    }

    places.emplace_back(SPlace{.label = "File System", .path = "/", .icon = "drive-harddisk"});
    return places;
}

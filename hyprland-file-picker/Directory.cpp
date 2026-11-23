#include "Directory.hpp"

#include <hyprutils/memory/Casts.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <sstream>

#include <sys/stat.h>

static bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

static unsigned char fold(char c) {
    if (c >= 'A' && c <= 'Z')
        c = c - 'A' + 'a';

    return c;
}

std::expected<std::vector<SFileEntry>, std::string> listDirectory(const std::filesystem::path& dir) {
    std::error_code                     ec;
    std::filesystem::directory_iterator it{dir, ec};
    if (ec)
        return std::unexpected(ec.message());

    std::vector<SFileEntry> entries;
    for (; it != std::filesystem::directory_iterator{}; it.increment(ec)) {
        if (ec)
            return std::unexpected(ec.message());

        auto name = it->path().filename().string();

        // the entry type comes from the directory listing, only symlinks need a stat here
        std::error_code typeEc;
        const bool      DIRECTORY = it->is_directory(typeEc) && !typeEc;
        const bool      HIDDEN    = name.starts_with('.');

        entries.emplace_back(SFileEntry{
            .name      = std::move(name),
            .directory = DIRECTORY,
            .hidden    = HIDDEN,
        });
    }

    if (ec)
        return std::unexpected(ec.message());

    sortEntries(entries);
    return entries;
}

void sortEntries(std::vector<SFileEntry>& entries) {
    std::ranges::sort(entries, [](const SFileEntry& a, const SFileEntry& b) {
        if (a.directory != b.directory)
            return a.directory;

        return naturalCompare(a.name, b.name) < 0;
    });
}

int naturalCompare(std::string_view a, std::string_view b) {
    size_t i = 0, j = 0;

    while (i < a.size() && j < b.size()) {
        if (isDigit(a[i]) && isDigit(b[j])) {
            size_t endA = i, endB = j;
            while (endA < a.size() && isDigit(a[endA])) {
                ++endA;
            }
            while (endB < b.size() && isDigit(b[endB])) {
                ++endB;
            }

            // without leading zeros, a longer run is a bigger number
            size_t startA = i, startB = j;
            while (startA + 1 < endA && a[startA] == '0') {
                ++startA;
            }
            while (startB + 1 < endB && b[startB] == '0') {
                ++startB;
            }

            if (endA - startA != endB - startB)
                return endA - startA < endB - startB ? -1 : 1;

            if (const auto CMP = a.substr(startA, endA - startA).compare(b.substr(startB, endB - startB)); CMP != 0)
                return CMP;

            i = endA;
            j = endB;
            continue;
        }

        if (fold(a[i]) != fold(b[j]))
            return fold(a[i]) < fold(b[j]) ? -1 : 1;

        ++i;
        ++j;
    }

    if (i < a.size() || j < b.size())
        return i < a.size() ? 1 : -1;

    // equal apart from case or leading zeros, keep the order stable
    return a.compare(b);
}

SFileDetails fileDetails(const std::filesystem::path& path) {
    struct stat st = {};
    if (stat(path.c_str(), &st) != 0)
        return {};

    return SFileDetails{
        .valid    = true,
        .size     = Hyprutils::Memory::sc<uintmax_t>(st.st_size),
        .modified = st.st_mtime,
    };
}

std::optional<std::string> readTextFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.good())
        return std::nullopt;

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string formatSize(uintmax_t bytes) {
    if (bytes == 1)
        return "1 byte";

    if (bytes < 1000)
        return std::format("{} bytes", bytes);

    static constexpr std::array<const char*, 5> UNITS = {"kB", "MB", "GB", "TB", "PB"};

    double                                      value = bytes / 1000.0;
    size_t                                      unit  = 0;

    // 999.95 would print as 1000.0
    while (value >= 999.95 && unit + 1 < UNITS.size()) {
        value /= 1000.0;
        ++unit;
    }

    return std::format("{:.1f} {}", value, UNITS[unit]);
}

std::string formatModified(const std::tm& time, const std::tm& now) {
    static constexpr std::array<const char*, 12> MONTHS = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    const auto                                   MONTH = MONTHS[std::clamp(time.tm_mon, 0, 11)];

    if (time.tm_year != now.tm_year)
        return std::format("{} {} {}", time.tm_mday, MONTH, time.tm_year + 1900);

    if (time.tm_yday != now.tm_yday)
        return std::format("{} {}", time.tm_mday, MONTH);

    return std::format("{:02}:{:02}", time.tm_hour, time.tm_min);
}

std::string formatModified(time_t time) {
    const time_t NOW = std::time(nullptr);

    std::tm      local = {}, nowLocal = {};
    if (!localtime_r(&time, &local) || !localtime_r(&NOW, &nowLocal))
        return "";

    return formatModified(local, nowLocal);
}

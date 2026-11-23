#pragma once

#include <cstdint>
#include <ctime>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct SFileEntry {
    std::string name;
    // symlinks to directories count as directories
    bool directory = false;
    bool hidden    = false;
};

struct SFileDetails {
    bool      valid    = false;
    uintmax_t size     = 0;
    time_t    modified = 0;
};

// sorted with sortEntries
std::expected<std::vector<SFileEntry>, std::string> listDirectory(const std::filesystem::path& dir);

// directories first, then natural, case-insensitive name order
void sortEntries(std::vector<SFileEntry>& entries);
// <0, 0 or >0 like strcmp, digit runs compare by value
int naturalCompare(std::string_view a, std::string_view b);

// one stat, follows symlinks
SFileDetails               fileDetails(const std::filesystem::path& path);

std::optional<std::string> readTextFile(const std::filesystem::path& path);

std::string                formatSize(uintmax_t bytes);
// "14:03" today, "3 Nov" this year, "3 Nov 2024" otherwise
std::string formatModified(const std::tm& time, const std::tm& now);
std::string formatModified(time_t time);

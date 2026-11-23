#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

enum ePickerMode : uint8_t {
    PICKER_MODE_OPEN = 0,
    PICKER_MODE_SAVE,
    PICKER_MODE_DIRECTORY,
};

struct SFilterRule {
    bool        mime = false;
    std::string pattern;
};

struct SFilter {
    std::string              name;
    std::vector<SFilterRule> rules;
};

struct SChoice {
    std::string label;
    // empty for a boolean choice
    std::vector<std::string> options;
    // "true" or "false" for a boolean choice, the option index otherwise
    std::string initial;
};

struct SPickerOptions {
    ePickerMode           mode     = PICKER_MODE_OPEN;
    bool                  multiple = false;
    std::string           title;
    std::string           acceptLabel;
    std::filesystem::path folder;
    std::string           name;
    std::vector<SFilter>  filters;
    size_t                currentFilter = 0;
    std::vector<SChoice>  choices;
};

struct SPickerResult {
    std::vector<std::filesystem::path> paths;
    std::optional<size_t>              filter;
    // one value per choice, formatted like SChoice::initial
    std::vector<std::string> choices;
};

/*
    --mode open|save|directory    --multiple
    --title TEXT                  --accept-label TEXT
    --folder PATH                 --name NAME
    --filter NAME, followed by any --glob PATTERN and --mime TYPE for it
    --current-filter INDEX
    --choice LABEL INITIAL, followed by any --choice-option LABEL for it
*/
std::expected<SPickerOptions, std::string> parsePickerOptions(std::span<const std::string_view> args);

/*
    One line each:
    [URI] file:///percent/encoded/path
    [FILTER] index
    [CHOICE] index value
*/
std::string formatPickerResult(const SPickerResult& result);

std::string pathToURI(const std::filesystem::path& path);
std::string escapeMarkup(std::string_view text);
// drops GTK style mnemonic underscores, "__" is a literal underscore
std::string stripMnemonics(std::string_view label);

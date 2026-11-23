#include "PickerOptions.hpp"

#include <hyprutils/memory/Casts.hpp>

#include <charconv>
#include <format>
#include <unordered_map>

static std::optional<size_t> parseIndex(std::string_view text) {
    size_t     value = 0;
    const auto END   = text.data() + text.size();
    const auto RES   = std::from_chars(text.data(), END, value);
    if (RES.ec != std::errc{} || RES.ptr != END)
        return std::nullopt;

    return value;
}

static std::optional<ePickerMode> parseMode(std::string_view text) {
    if (text == "open")
        return PICKER_MODE_OPEN;
    if (text == "save")
        return PICKER_MODE_SAVE;
    if (text == "directory")
        return PICKER_MODE_DIRECTORY;

    return std::nullopt;
}

// invalid initial values fall back to the first option, or false
static void sanitizeChoice(SChoice& choice) {
    if (choice.options.empty()) {
        if (choice.initial != "true")
            choice.initial = "false";
        return;
    }

    const auto INDEX = parseIndex(choice.initial);
    if (!INDEX || *INDEX >= choice.options.size())
        choice.initial = "0";
}

std::expected<SPickerOptions, std::string> parsePickerOptions(std::span<const std::string_view> args) {
    static const std::unordered_map<std::string_view, size_t> VALUE_COUNTS = {
        {"--mode", 1},   {"--multiple", 0}, {"--title", 1}, {"--accept-label", 1},   {"--folder", 1}, {"--name", 1},
        {"--filter", 1}, {"--glob", 1},     {"--mime", 1},  {"--current-filter", 1}, {"--choice", 2}, {"--choice-option", 1},
    };

    SPickerOptions        options;
    std::optional<size_t> currentFilter;

    for (size_t i = 0; i < args.size(); ++i) {
        const auto ARG = args[i];

        const auto COUNT = VALUE_COUNTS.find(ARG);
        if (COUNT == VALUE_COUNTS.end())
            return std::unexpected(std::format("unknown option {}", ARG));

        if (i + COUNT->second >= args.size())
            return std::unexpected(std::format("{} needs {} value(s)", ARG, COUNT->second));

        const auto VALUES = args.subspan(i + 1, COUNT->second);
        i += COUNT->second;

        if (ARG == "--mode") {
            const auto MODE = parseMode(VALUES[0]);
            if (!MODE)
                return std::unexpected(std::format("unknown mode {}", VALUES[0]));

            options.mode = *MODE;
        } else if (ARG == "--multiple")
            options.multiple = true;
        else if (ARG == "--title")
            options.title = VALUES[0];
        else if (ARG == "--accept-label")
            options.acceptLabel = VALUES[0];
        else if (ARG == "--folder")
            options.folder = VALUES[0];
        else if (ARG == "--name")
            options.name = VALUES[0];
        else if (ARG == "--filter")
            options.filters.emplace_back(SFilter{.name = std::string{VALUES[0]}});
        else if (ARG == "--glob" || ARG == "--mime") {
            if (options.filters.empty())
                return std::unexpected(std::format("{} needs a --filter before it", ARG));

            options.filters.back().rules.emplace_back(SFilterRule{
                .mime    = ARG == "--mime",
                .pattern = std::string{VALUES[0]},
            });
        } else if (ARG == "--current-filter") {
            currentFilter = parseIndex(VALUES[0]);
            if (!currentFilter)
                return std::unexpected(std::format("invalid filter index {}", VALUES[0]));
        } else if (ARG == "--choice")
            options.choices.emplace_back(SChoice{.label = std::string{VALUES[0]}, .initial = std::string{VALUES[1]}});
        else if (ARG == "--choice-option") {
            if (options.choices.empty())
                return std::unexpected("--choice-option needs a --choice before it");

            options.choices.back().options.emplace_back(VALUES[0]);
        }
    }

    if (currentFilter && *currentFilter < options.filters.size())
        options.currentFilter = *currentFilter;

    for (auto& choice : options.choices) {
        sanitizeChoice(choice);
    }

    return options;
}

std::string formatPickerResult(const SPickerResult& result) {
    std::string output;

    for (const auto& path : result.paths) {
        output += std::format("[URI] {}\n", pathToURI(path));
    }

    if (result.filter)
        output += std::format("[FILTER] {}\n", *result.filter);

    for (size_t i = 0; i < result.choices.size(); ++i) {
        output += std::format("[CHOICE] {} {}\n", i, result.choices[i]);
    }

    return output;
}

std::string pathToURI(const std::filesystem::path& path) {
    std::string uri = "file://";

    for (const char c : path.native()) {
        const bool UNRESERVED = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~' || c == '/';

        if (UNRESERVED)
            uri += c;
        else
            uri += std::format("%{:02X}", Hyprutils::Memory::sc<unsigned char>(c));
    }

    return uri;
}

std::string escapeMarkup(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size());

    for (const char c : text) {
        switch (c) {
            case '&': escaped += "&amp;"; break;
            case '<': escaped += "&lt;"; break;
            case '>': escaped += "&gt;"; break;
            case '"': escaped += "&quot;"; break;
            case '\'': escaped += "&apos;"; break;
            default: escaped += c; break;
        }
    }

    return escaped;
}

std::string stripMnemonics(std::string_view label) {
    std::string stripped;
    stripped.reserve(label.size());

    for (size_t i = 0; i < label.size(); ++i) {
        if (label[i] != '_') {
            stripped += label[i];
            continue;
        }

        // "__" is an escaped underscore, a single one marks the mnemonic
        if (i + 1 < label.size() && label[i + 1] == '_') {
            stripped += '_';
            ++i;
        }
    }

    return stripped;
}

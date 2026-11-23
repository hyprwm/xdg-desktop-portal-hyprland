#include "FileChooserData.hpp"

#include <hyprutils/memory/Casts.hpp>

#include <algorithm>
#include <charconv>
#include <format>
#include <ranges>

using Hyprutils::Memory::sc;

static std::optional<size_t> parseIndex(std::string_view text) {
    size_t     value = 0;
    const auto END   = text.data() + text.size();
    const auto RES   = std::from_chars(text.data(), END, value);
    if (RES.ec != std::errc{} || RES.ptr != END)
        return std::nullopt;

    return value;
}

// a lone current filter is the only filter there is
static std::vector<SFileChooserFilter> pickerFilters(const SFileChooserRequest& request) {
    if (request.filters.empty() && request.currentFilter)
        return {*request.currentFilter};

    return request.filters;
}

static std::optional<size_t> currentFilterIndex(const SFileChooserRequest& request) {
    if (!request.currentFilter)
        return std::nullopt;

    if (request.filters.empty())
        return 0;

    // like the GTK backend, a filter with the same name is the same filter
    const auto IT = std::ranges::find(request.filters, request.currentFilter->name, &SFileChooserFilter::name);
    if (IT == request.filters.end())
        return std::nullopt;

    return sc<size_t>(std::distance(request.filters.begin(), IT));
}

static std::string initialChoiceValue(const SFileChooserChoice& choice) {
    if (choice.options.empty())
        return choice.initial == "true" ? "true" : "false";

    const auto IT = std::ranges::find(choice.options, choice.initial, &std::pair<std::string, std::string>::first);
    return std::to_string(IT == choice.options.end() ? 0 : std::distance(choice.options.begin(), IT));
}

static void append(std::vector<std::string>& args, std::initializer_list<std::string_view> values) {
    for (const auto VALUE : values) {
        args.emplace_back(VALUE);
    }
}

std::vector<std::string> buildFilePickerArgs(const SFileChooserRequest& request) {
    std::vector<std::string> args;

    const bool               PICK_DIRECTORY = request.method == FILE_CHOOSER_SAVE_FILES || (request.method == FILE_CHOOSER_OPEN_FILE && request.directory);
    append(args, {"--mode", request.method == FILE_CHOOSER_SAVE_FILE ? "save" : (PICK_DIRECTORY ? "directory" : "open")});

    if (request.method == FILE_CHOOSER_OPEN_FILE && request.multiple)
        args.emplace_back("--multiple");

    if (!request.title.empty())
        append(args, {"--title", request.title});

    // SaveFiles picks a folder, but what the user does is saving
    const std::string ACCEPT = request.acceptLabel.empty() && request.method == FILE_CHOOSER_SAVE_FILES ? "Save" : request.acceptLabel;
    if (!ACCEPT.empty())
        append(args, {"--accept-label", ACCEPT});

    // an existing file decides both where and what to save
    if (request.method == FILE_CHOOSER_SAVE_FILE && !request.currentFile.empty()) {
        const std::filesystem::path FILE = request.currentFile;
        append(args, {"--folder", FILE.parent_path().string(), "--name", FILE.filename().string()});
    } else {
        if (!request.currentFolder.empty())
            append(args, {"--folder", request.currentFolder});

        if (request.method == FILE_CHOOSER_SAVE_FILE && !request.currentName.empty())
            append(args, {"--name", request.currentName});
    }

    for (const auto& filter : pickerFilters(request)) {
        append(args, {"--filter", filter.name});

        for (const auto& [type, pattern] : filter.rules) {
            if (type > 1)
                continue;

            append(args, {type == 0 ? "--glob" : "--mime", pattern});
        }
    }

    if (const auto INDEX = currentFilterIndex(request); INDEX)
        append(args, {"--current-filter", std::to_string(*INDEX)});

    for (const auto& choice : request.choices) {
        append(args, {"--choice", choice.label, initialChoiceValue(choice)});

        for (const auto& option : choice.options) {
            append(args, {"--choice-option", option.second});
        }
    }

    return args;
}

std::optional<SFileChooserResponse> parseFilePickerOutput(std::string_view output, const SFileChooserRequest& request,
                                                          const std::function<bool(const std::filesystem::path&)>& exists) {
    SFileChooserResponse     response;
    const auto               FILTERS = pickerFilters(request);

    std::vector<std::string> choiceValues;
    for (const auto& choice : request.choices) {
        choiceValues.emplace_back(initialChoiceValue(choice));
    }

    for (const auto RAW : output | std::views::split('\n')) {
        const std::string_view LINE{RAW.begin(), RAW.end()};

        if (LINE.starts_with("[URI] ")) {
            // anything that isn't a local file gets dropped, per the spec
            if (const auto URI = LINE.substr(6); fileURIToPath(URI))
                response.uris.emplace_back(URI);
        } else if (LINE.starts_with("[FILTER] ")) {
            if (const auto INDEX = parseIndex(LINE.substr(9)); INDEX && *INDEX < FILTERS.size())
                response.currentFilter = FILTERS[*INDEX];
        } else if (LINE.starts_with("[CHOICE] ")) {
            const auto VALUE = LINE.substr(9);
            const auto SPACE = VALUE.find(' ');
            const auto INDEX = parseIndex(VALUE.substr(0, SPACE));
            if (SPACE != std::string_view::npos && INDEX && *INDEX < choiceValues.size())
                choiceValues[*INDEX] = VALUE.substr(SPACE + 1);
        }
    }

    if (response.uris.empty())
        return std::nullopt;

    if (request.method != FILE_CHOOSER_OPEN_FILE || !request.multiple)
        response.uris.resize(1);

    for (size_t i = 0; i < request.choices.size(); ++i) {
        const auto& CHOICE = request.choices[i];

        if (CHOICE.options.empty()) {
            response.choices.emplace_back(CHOICE.id, choiceValues[i] == "true" ? "true" : "false");
            continue;
        }

        const auto OPTION = parseIndex(choiceValues[i]).value_or(0);
        response.choices.emplace_back(CHOICE.id, CHOICE.options[OPTION < CHOICE.options.size() ? OPTION : 0].first);
    }

    // SaveFiles picks the folder, the files go inside it
    if (request.method == FILE_CHOOSER_SAVE_FILES) {
        const auto DIR = fileURIToPath(response.uris.front());
        response.uris.clear();

        for (const auto& name : request.files) {
            response.uris.emplace_back(pathToFileURI(uniqueFilePath(*DIR, name, exists)));
        }
    }

    return response;
}

static std::optional<uint8_t> hexValue(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;

    return std::nullopt;
}

std::optional<std::filesystem::path> fileURIToPath(std::string_view uri) {
    if (!uri.starts_with("file:///"))
        return std::nullopt;

    const auto  ENCODED = uri.substr(7);

    std::string path;
    path.reserve(ENCODED.size());
    for (size_t i = 0; i < ENCODED.size(); ++i) {
        if (ENCODED[i] != '%') {
            path += ENCODED[i];
            continue;
        }

        const auto HIGH = i + 2 < ENCODED.size() ? hexValue(ENCODED[i + 1]) : std::nullopt;
        const auto LOW  = i + 2 < ENCODED.size() ? hexValue(ENCODED[i + 2]) : std::nullopt;
        if (!HIGH || !LOW)
            return std::nullopt;

        path += sc<char>((*HIGH << 4) | *LOW);
        i += 2;
    }

    return path;
}

std::string pathToFileURI(const std::filesystem::path& path) {
    std::string uri = "file://";

    for (const char c : path.native()) {
        const bool UNRESERVED = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~' || c == '/';

        if (UNRESERVED)
            uri += c;
        else
            uri += std::format("%{:02X}", sc<unsigned char>(c));
    }

    return uri;
}

std::filesystem::path uniqueFilePath(const std::filesystem::path& dir, std::string_view name, const std::function<bool(const std::filesystem::path&)>& exists) {
    // the name comes from the app, it must not point anywhere outside dir
    std::string base = std::filesystem::path{name}.filename().string();
    if (base.empty() || base == "." || base == "..")
        base = "untitled";

    if (!exists(dir / base))
        return dir / base;

    // "a.tar.gz" becomes "a (1).tar.gz", a leading dot belongs to the stem
    const auto DOT    = base.find('.', 1);
    const auto STEM   = std::string_view{base}.substr(0, DOT);
    const auto SUFFIX = DOT == std::string::npos ? std::string_view{} : std::string_view{base}.substr(DOT);

    for (size_t i = 1; i < 10000; ++i) {
        auto candidate = dir / std::format("{} ({}){}", STEM, i, SUFFIX);
        if (!exists(candidate))
            return candidate;
    }

    return dir / base;
}

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Plain mirrors of the org.freedesktop.impl.portal.FileChooser data, and the translation to and from hyprland-file-picker

enum eFileChooserMethod : uint8_t {
    FILE_CHOOSER_OPEN_FILE = 0,
    FILE_CHOOSER_SAVE_FILE,
    FILE_CHOOSER_SAVE_FILES,
};

// (sa(us)), a rule of type 0 is a glob, 1 a mime type
struct SFileChooserFilter {
    std::string                                   name;
    std::vector<std::pair<uint32_t, std::string>> rules;

    bool                                          operator==(const SFileChooserFilter&) const = default;
};

// (ssa(ss)s), without options it's a boolean choice
struct SFileChooserChoice {
    std::string                                      id;
    std::string                                      label;
    std::vector<std::pair<std::string, std::string>> options;
    std::string                                      initial;
};

struct SFileChooserRequest {
    eFileChooserMethod                method = FILE_CHOOSER_OPEN_FILE;
    std::string                       title;
    std::string                       acceptLabel;
    bool                              multiple  = false;
    bool                              directory = false;
    std::vector<SFileChooserFilter>   filters;
    std::optional<SFileChooserFilter> currentFilter;
    std::vector<SFileChooserChoice>   choices;
    std::string                       currentName;
    std::string                       currentFolder;
    std::string                       currentFile;
    // SaveFiles only
    std::vector<std::string> files;
};

struct SFileChooserResponse {
    std::vector<std::string>          uris;
    std::optional<SFileChooserFilter> currentFilter;
    // (choice id, option id), "true" or "false" for boolean choices
    std::vector<std::pair<std::string, std::string>> choices;
};

std::vector<std::string> buildFilePickerArgs(const SFileChooserRequest& request);

// nullopt when nothing was picked. exists is consulted to keep SaveFiles from overwriting files.
std::optional<SFileChooserResponse> parseFilePickerOutput(std::string_view output, const SFileChooserRequest& request,
                                                          const std::function<bool(const std::filesystem::path&)>& exists);

// local file:// URIs only
std::optional<std::filesystem::path> fileURIToPath(std::string_view uri);
std::string                          pathToFileURI(const std::filesystem::path& path);

// "a.tar.gz" in a folder that has it already becomes "a (1).tar.gz"
std::filesystem::path uniqueFilePath(const std::filesystem::path& dir, std::string_view name, const std::function<bool(const std::filesystem::path&)>& exists);

#include "../src/shared/FileChooserData.hpp"

#include <algorithm>
#include <iostream>
#include <set>

static bool expect(bool condition, const char* message) {
    if (condition)
        return true;

    std::cerr << message << '\n';
    return false;
}

static bool contains(const std::vector<std::string>& args, const std::vector<std::string>& sequence) {
    return std::ranges::search(args, sequence).begin() != args.end();
}

static const SFileChooserFilter IMAGES = {.name = "Images", .rules = {{0, "*.png"}, {1, "image/jpeg"}, {7, "ignored"}}};
static const SFileChooserFilter TEXT   = {.name = "Text", .rules = {{1, "text/plain"}}};

static bool                     testArgs() {
    bool                      success = true;

    const SFileChooserRequest OPEN = {
        .method        = FILE_CHOOSER_OPEN_FILE,
        .title         = "Open Image",
        .acceptLabel   = "_Open",
        .multiple      = true,
        .filters       = {IMAGES, TEXT},
        .currentFilter = SFileChooserFilter{.name = "Text"},
        .choices       = {{.id = "enc", .label = "Encoding", .options = {{"utf8", "UTF-8"}, {"latin1", "Latin-1"}}, .initial = "latin1"},
                          {.id = "ro", .label = "Read only", .initial = "true"}},
        .currentFolder = "/home/user",
    };

    const auto ARGS = buildFilePickerArgs(OPEN);
    success &= expect(contains(ARGS, {"--mode", "open", "--multiple", "--title", "Open Image", "--accept-label", "_Open", "--folder", "/home/user"}), "basic options are wrong");
    success &= expect(contains(ARGS, {"--filter", "Images", "--glob", "*.png", "--mime", "image/jpeg", "--filter", "Text"}), "filters are wrong");
    success &= expect(!std::ranges::contains(ARGS, "ignored"), "unknown filter rule types must be skipped");
    success &= expect(contains(ARGS, {"--current-filter", "1"}), "the current filter must be found by name");
    success &=
        expect(contains(ARGS, {"--choice", "Encoding", "1", "--choice-option", "UTF-8", "--choice-option", "Latin-1", "--choice", "Read only", "true"}), "choices are wrong");

    const SFileChooserRequest DIRECTORY = {.method = FILE_CHOOSER_OPEN_FILE, .directory = true, .currentFilter = TEXT};
    const auto                DIR_ARGS  = buildFilePickerArgs(DIRECTORY);
    success &= expect(contains(DIR_ARGS, {"--mode", "directory"}) && !std::ranges::contains(DIR_ARGS, "--multiple"), "directory mode is wrong");
    success &= expect(contains(DIR_ARGS, {"--filter", "Text", "--mime", "text/plain", "--current-filter", "0"}), "a lone current filter must become the only filter");

    const SFileChooserRequest SAVE = {.method = FILE_CHOOSER_SAVE_FILE, .currentName = "new.txt", .currentFolder = "/tmp", .currentFile = "/home/user/old.txt"};
    success &= expect(contains(buildFilePickerArgs(SAVE), {"--mode", "save", "--folder", "/home/user", "--name", "old.txt"}), "the current file must win over name and folder");

    const SFileChooserRequest SAVE_NAME = {.method = FILE_CHOOSER_SAVE_FILE, .currentName = "new.txt", .currentFolder = "/tmp"};
    success &= expect(contains(buildFilePickerArgs(SAVE_NAME), {"--folder", "/tmp", "--name", "new.txt"}), "current name and folder are wrong");

    const SFileChooserRequest SAVE_FILES = {.method = FILE_CHOOSER_SAVE_FILES, .multiple = true};
    const auto                FILES_ARGS = buildFilePickerArgs(SAVE_FILES);
    success &=
        expect(contains(FILES_ARGS, {"--mode", "directory", "--accept-label", "Save"}) && !std::ranges::contains(FILES_ARGS, "--multiple"), "SaveFiles must pick one folder");

    const SFileChooserRequest UNKNOWN_CURRENT = {.filters = {IMAGES}, .currentFilter = TEXT};
    success &= expect(!std::ranges::contains(buildFilePickerArgs(UNKNOWN_CURRENT), "--current-filter"), "an unknown current filter must be ignored");

    return success;
}

static bool testOutput() {
    bool                      success = true;
    const auto                NONE    = [](const std::filesystem::path&) { return false; };

    const SFileChooserRequest REQUEST = {
        .method   = FILE_CHOOSER_OPEN_FILE,
        .multiple = true,
        .filters  = {IMAGES, TEXT},
        .choices  = {{.id = "enc", .label = "Encoding", .options = {{"utf8", "UTF-8"}, {"latin1", "Latin-1"}}, .initial = "utf8"},
                     {.id = "ro", .label = "Read only", .initial = "false"},
                     {.id = "untouched", .label = "Untouched", .options = {{"a", "A"}, {"b", "B"}}, .initial = "b"}},
    };

    const auto RESPONSE = parseFilePickerOutput("[URI] file:///a%20b.png\n[URI] https://example.com/x\n[URI] file:///c.png\n"
                                                "[FILTER] 1\n[CHOICE] 0 1\n[CHOICE] 1 true\n[CHOICE] 9 1\ngarbage\n",
                                                REQUEST, NONE);
    success &= expect(RESPONSE.has_value(), "a valid output was rejected");
    if (RESPONSE) {
        success &= expect(RESPONSE->uris == std::vector<std::string>{"file:///a%20b.png", "file:///c.png"}, "non-file URIs must be dropped");
        success &= expect(RESPONSE->currentFilter == TEXT, "the picked filter must be the original one");
        success &= expect(RESPONSE->choices == std::vector<std::pair<std::string, std::string>>{{"enc", "latin1"}, {"ro", "true"}, {"untouched", "b"}},
                          "choices must map back to ids, untouched ones keep their initial value");
    }

    success &= expect(!parseFilePickerOutput("", REQUEST, NONE) && !parseFilePickerOutput("[FILTER] 0\n", REQUEST, NONE), "nothing picked must mean cancelled");

    const SFileChooserRequest SINGLE = {.method = FILE_CHOOSER_OPEN_FILE};
    const auto                ONE    = parseFilePickerOutput("[URI] file:///a\n[URI] file:///b\n", SINGLE, NONE);
    success &= expect(ONE && ONE->uris.size() == 1 && !ONE->currentFilter, "a single selection must return one URI");

    return success;
}

static bool testSaveFiles() {
    bool                           success = true;

    const std::set<std::string>    EXISTING = {"/dst/a.tar.gz", "/dst/a (1).tar.gz", "/dst/.hidden", "/dst/plain"};
    const auto                     EXISTS   = [&EXISTING](const std::filesystem::path& path) { return EXISTING.contains(path.string()); };

    const SFileChooserRequest      REQUEST  = {.method = FILE_CHOOSER_SAVE_FILES, .files = {"a.tar.gz", ".hidden", "plain", "new.txt", "../../escape", ".."}};
    const auto                     RESPONSE = parseFilePickerOutput("[URI] file:///dst\n", REQUEST, EXISTS);

    const std::vector<std::string> EXPECTED = {
        "file:///dst/a%20%282%29.tar.gz", "file:///dst/.hidden%20%281%29", "file:///dst/plain%20%281%29", "file:///dst/new.txt", "file:///dst/escape", "file:///dst/untitled"};
    success &= expect(RESPONSE && RESPONSE->uris == EXPECTED, "SaveFiles must append unique names to the picked folder and never leave it");

    return success;
}

static bool testURIs() {
    bool success = true;

    success &= expect(pathToFileURI("/home/a b/é#1") == "file:///home/a%20b/%C3%A9%231", "URIs are not encoded correctly");
    success &= expect(fileURIToPath("file:///home/a%20b/%C3%A9%231") == std::filesystem::path{"/home/a b/é#1"}, "URIs are not decoded correctly");
    success &= expect(!fileURIToPath("file://host/x") && !fileURIToPath("http:///x") && !fileURIToPath("file:///bad%2"), "invalid URIs must be rejected");

    return success;
}

int main() {
    bool success = true;

    success &= testArgs();
    success &= testOutput();
    success &= testSaveFiles();
    success &= testURIs();

    return success ? 0 : 1;
}

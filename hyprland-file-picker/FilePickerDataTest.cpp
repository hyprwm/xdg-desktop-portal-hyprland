#include "Directory.hpp"
#include "Glob.hpp"
#include "MimeDatabase.hpp"
#include "PickerOptions.hpp"
#include "Places.hpp"
#include "QuickSearch.hpp"

#include <array>
#include <fstream>
#include <iostream>
#include <unistd.h>

static bool expect(bool condition, const char* message) {
    if (condition)
        return true;

    std::cerr << message << '\n';
    return false;
}

static bool testGlob() {
    bool success = true;

    success &= expect(globMatch("*.png", "cat.png") && !globMatch("*.png", "cat.pngs") && !globMatch("*.png", "cat.PNG"), "glob suffix matching is wrong");
    success &= expect(globMatch("*.png", "cat.PNG", false) && globMatch("*.PNG", "cat.png", false), "case-insensitive glob matching is wrong");
    success &= expect(globMatch("a?c", "abc") && !globMatch("a?c", "ac"), "? must match exactly one byte");
    success &= expect(globMatch("*", "") && globMatch("**.c", "x.c") && globMatch("a*b*c", "aXbYbZc") && !globMatch("a*b*c", "aXbY"), "* backtracking is wrong");
    success &= expect(globMatch("[abc]x", "bx") && !globMatch("[abc]x", "dx") && globMatch("[!abc]x", "dx") && globMatch("[^abc]x", "dx"), "glob classes are wrong");
    success &= expect(globMatch("*.[0-9]", "ls.1") && !globMatch("*.[0-9]", "ls.a") && globMatch("[A-Z]", "q", false), "glob class ranges are wrong");
    success &= expect(globMatch("[]]", "]") && globMatch("a[b", "a[b") && !globMatch("a[b", "ab"), "literal brackets are wrong");

    return success;
}

static bool testMimeDatabase() {
    bool          success = true;

    CMimeDatabase db;
    db.parseGlobs("# comment\n"
                  "50:image/png:*.png\n"
                  "50:text/x-csrc:*.c\n"
                  "50:text/x-c++src:*.C:cs\n"
                  "50:application/x-compressed-tar:*.tar.gz\n"
                  "50:application/gzip:*.gz\n"
                  "50:text/markdown:*.md\n"
                  "50:text/x-makefile:Makefile\n"
                  "10:text/x-readme:README*\n"
                  "0:text/x-dropped:__NOGLOBS__\n");
    db.parseAliases("application/x-gzip application/gzip\n");
    db.parseSubclasses("application/x-compressed-tar application/gzip\n"
                       "text/x-csrc text/plain\n");
    db.parseGenericIcons("image/png:image-x-generic\n");

    success &= expect(db.typeForName("cat.png") == "image/png" && db.typeForName("CAT.PNG") == "image/png", "suffix lookup must be case-insensitive");
    success &= expect(db.typeForName("main.c") == "text/x-csrc" && db.typeForName("main.C") == "text/x-c++src", "case-sensitive globs must win over case-insensitive ones");
    success &= expect(db.typeForName("a.tar.gz") == "application/x-compressed-tar" && db.typeForName("a.gz") == "application/gzip", "the longest suffix must win");
    success &= expect(db.typeForName("README.md") == "text/markdown" && db.typeForName("README") == "text/x-readme", "suffixes must win over weaker globs");
    success &= expect(db.typeForName("Makefile") == "text/x-makefile" && db.typeForName("unknown.qqq").empty(), "literal or unknown names are wrong");

    success &= expect(db.isA("image/png", "image/png") && db.isA("image/png", "image/*") && !db.isA("image/png", "text/*"), "wildcard matching is wrong");
    success &= expect(db.isA("application/x-compressed-tar", "application/x-gzip"), "subclass matching must resolve aliases");
    success &= expect(db.isA("text/x-csrc", "text/plain") && db.isA("text/markdown", "text/plain") && !db.isA("image/png", "text/plain"), "text/plain parents are wrong");
    success &= expect(db.isA("", "application/octet-stream") && !db.isA("", "image/*"), "unknown types must only be octet streams");

    success &= expect(db.iconsForType("image/png") == std::vector<std::string>{"image-png", "image-x-generic", "text-x-generic"}, "generic icons are wrong");
    success &= expect(db.iconsForType("text/markdown") == std::vector<std::string>{"text-markdown", "text-x-generic"}, "media type fallback icons are wrong");
    success &= expect(db.iconsForType("") == std::vector<std::string>{"text-x-generic"}, "unknown types need a fallback icon");

    const SFilter IMAGES = {.name = "Images", .rules = {{.pattern = "*.jpg"}, {.mime = true, .pattern = "image/*"}}};
    const SFilter TEXT   = {.name = "Text", .rules = {{.mime = true, .pattern = "text/plain"}}};
    success &= expect(matchesFilter(IMAGES, "a.PNG", db) && matchesFilter(IMAGES, "b.JPG", db) && !matchesFilter(IMAGES, "c.md", db), "image filters are wrong");
    success &= expect(matchesFilter(TEXT, "main.c", db) && !matchesFilter(TEXT, "unknown.qqq", db) && matchesFilter({.name = "All"}, "x", db), "text filters are wrong");

    return success;
}

static bool testOptions() {
    bool                                       success = true;

    constexpr std::array<std::string_view, 22> ARGS = {
        "--mode", "save",   "--multiple", "--title", "Export",     "--accept-label", "_Export", "--folder",         "/tmp", "--name",     "a.png", "--filter",
        "Images", "--glob", "*.png",      "--mime",  "image/jpeg", "--filter",       "All",     "--current-filter", "1",    "--multiple",
    };

    const auto OPTIONS = parsePickerOptions(ARGS);
    success &= expect(OPTIONS.has_value(), "valid options were rejected");
    if (OPTIONS) {
        success &= expect(OPTIONS->mode == PICKER_MODE_SAVE && OPTIONS->multiple && OPTIONS->title == "Export" && OPTIONS->acceptLabel == "_Export", "scalar options are wrong");
        success &= expect(OPTIONS->folder == "/tmp" && OPTIONS->name == "a.png" && OPTIONS->currentFilter == 1, "path options are wrong");
        success &= expect(OPTIONS->filters.size() == 2 && OPTIONS->filters[0].rules.size() == 2 && OPTIONS->filters[1].rules.empty(), "filters were grouped wrong");
        success &=
            expect(!OPTIONS->filters[0].rules[0].mime && OPTIONS->filters[0].rules[1].mime && OPTIONS->filters[0].rules[1].pattern == "image/jpeg", "filter rules are wrong");
    }

    constexpr std::array<std::string_view, 12> CHOICE_ARGS = {
        "--choice", "Encoding", "1", "--choice-option", "UTF-8", "--choice-option", "Latin-1", "--choice", "Strip", "yes", "--current-filter", "4",
    };

    const auto CHOICES = parsePickerOptions(CHOICE_ARGS);
    success &= expect(CHOICES && CHOICES->choices.size() == 2, "choices were rejected");
    if (CHOICES && CHOICES->choices.size() == 2) {
        success &= expect(CHOICES->choices[0].options.size() == 2 && CHOICES->choices[0].initial == "1", "list choices are wrong");
        success &= expect(CHOICES->choices[1].options.empty() && CHOICES->choices[1].initial == "false", "invalid boolean choices must fall back to false");
        success &= expect(CHOICES->currentFilter == 0, "an out of range filter must fall back to the first one");
    }

    constexpr std::array<std::string_view, 1> UNKNOWN = {"--bogus"};
    constexpr std::array<std::string_view, 1> MISSING = {"--title"};
    constexpr std::array<std::string_view, 2> ORPHAN  = {"--glob", "*.png"};
    constexpr std::array<std::string_view, 2> MODE    = {"--mode", "nope"};
    success &= expect(!parsePickerOptions(UNKNOWN) && !parsePickerOptions(MISSING) && !parsePickerOptions(ORPHAN) && !parsePickerOptions(MODE), "invalid options were accepted");

    return success;
}

static bool testOutput() {
    bool success = true;

    success &= expect(pathToURI("/home/a b/é#1.txt") == "file:///home/a%20b/%C3%A9%231.txt", "URIs are not percent-encoded correctly");
    success &= expect(pathToURI("/tmp/line\nbreak") == "file:///tmp/line%0Abreak", "newlines must never reach the output");

    const SPickerResult RESULT = {
        .paths   = {"/a", "/b c"},
        .filter  = 2,
        .choices = {"true", "1"},
    };
    success &= expect(formatPickerResult(RESULT) == "[URI] file:///a\n[URI] file:///b%20c\n[FILTER] 2\n[CHOICE] 0 true\n[CHOICE] 1 1\n", "result output is wrong");
    success &= expect(formatPickerResult({}).empty(), "a cancelled result must print nothing");

    success &= expect(escapeMarkup("<a & 'b'>") == "&lt;a &amp; &apos;b&apos;&gt;", "markup escaping is wrong");
    success &= expect(stripMnemonics("_Open") == "Open" && stripMnemonics("Save__as") == "Save_as", "mnemonics are not stripped correctly");

    return success;
}

static bool testSorting() {
    bool success = true;

    success &= expect(naturalCompare("file2", "file10") < 0 && naturalCompare("img_10.png", "img_9.png") > 0, "digit runs must compare by value");
    success &= expect(naturalCompare("a", "B") < 0 && naturalCompare("B", "a") > 0 && naturalCompare("", "a") < 0, "names must compare case-insensitively");
    success &= expect(naturalCompare("x01", "x1") != 0 && naturalCompare("Abc", "abc") != 0 && naturalCompare("same", "same") == 0, "only identical names may compare equal");

    std::vector<SFileEntry> entries = {
        {.name = "b.txt"},
        {.name = "c", .directory = true},
        {.name = "a.txt"},
        {.name = "A", .directory = true},
    };
    sortEntries(entries);
    success &= expect(entries[0].name == "A" && entries[1].name == "c" && entries[2].name == "a.txt" && entries[3].name == "b.txt", "directories must sort first");

    return success;
}

static bool testFormatting() {
    bool success = true;

    success &= expect(formatSize(0) == "0 bytes" && formatSize(1) == "1 byte" && formatSize(999) == "999 bytes", "small sizes are wrong");
    success &= expect(formatSize(1000) == "1.0 kB" && formatSize(1234567) == "1.2 MB" && formatSize(999950) == "1.0 MB", "large sizes are wrong");

    const std::tm NOW       = {.tm_min = 30, .tm_hour = 18, .tm_mday = 23, .tm_mon = 10, .tm_year = 126, .tm_yday = 326};
    const std::tm TODAY     = {.tm_min = 5, .tm_hour = 9, .tm_mday = 23, .tm_mon = 10, .tm_year = 126, .tm_yday = 326};
    const std::tm THIS_YEAR = {.tm_mday = 3, .tm_mon = 10, .tm_year = 126, .tm_yday = 306};
    const std::tm OLDER     = {.tm_mday = 3, .tm_mon = 10, .tm_year = 124, .tm_yday = 307};
    success &= expect(formatModified(TODAY, NOW) == "09:05" && formatModified(THIS_YEAR, NOW) == "3 Nov" && formatModified(OLDER, NOW) == "3 Nov 2024",
                      "modification times are formatted wrong");

    return success;
}

static bool testListing() {
    bool       success = true;

    const auto DIR = std::filesystem::temp_directory_path() / ("hyprland-file-picker-test-" + std::to_string(getpid()));
    std::filesystem::create_directories(DIR / "sub");
    std::ofstream(DIR / "b") << "hello";
    std::ofstream(DIR / ".hidden");
    std::filesystem::create_directory_symlink(DIR / "sub", DIR / "link");

    const auto ENTRIES = listDirectory(DIR);
    success &= expect(ENTRIES && ENTRIES->size() == 4, "listing a directory failed");
    if (ENTRIES && ENTRIES->size() == 4) {
        success &= expect((*ENTRIES)[0].name == "link" && (*ENTRIES)[0].directory && (*ENTRIES)[1].name == "sub", "symlinked directories must list as directories");
        success &= expect((*ENTRIES)[2].name == ".hidden" && (*ENTRIES)[2].hidden && !(*ENTRIES)[3].hidden, "hidden files are detected wrong");
    }

    const auto DETAILS = fileDetails(DIR / "b");
    success &= expect(DETAILS.valid && DETAILS.size == 5 && DETAILS.modified > 0, "file details are wrong");
    success &= expect(!fileDetails(DIR / "missing").valid && !listDirectory(DIR / "missing"), "missing paths must fail");

    std::filesystem::remove_all(DIR);
    return success;
}

static bool testUserDirs() {
    const auto DIRS = parseUserDirs("# comment\n"
                                    "XDG_DESKTOP_DIR=\"$HOME/Desktop\"\n"
                                    "XDG_DOWNLOAD_DIR=\"/mnt/downloads\"\n"
                                    "XDG_MUSIC_DIR=\"$HOME\"\n"
                                    "  XDG_PICTURES_DIR=\"$HOME/My Pictures\"\n"
                                    "XDG_VIDEOS_DIR=$HOME/Videos\n"
                                    "XDG_BOGUS=\"/x\"\n",
                                    "/home/user");

    return expect(DIRS.size() == 4 && DIRS.at("DESKTOP") == "/home/user/Desktop" && DIRS.at("DOWNLOAD") == "/mnt/downloads" && DIRS.at("MUSIC") == "/home/user" &&
                      DIRS.at("PICTURES") == "/home/user/My Pictures",
                  "user-dirs.dirs is parsed wrong");
}

static bool testQuickSearch() {
    bool                          success = true;

    const std::vector<SFileEntry> ENTRIES = {
        {.name = "Desktop", .directory = true},
        {.name = "Documents", .directory = true},
        {.name = "hello.txt"},
        {.name = "help.md"},
        {.name = "Hexagon.png"},
        {.name = "notes.txt"},
        {.name = "q3 report.pdf"},
    };

    success &= expect(findQuickSearchMatch(ENTRIES, "hel", 0) == 2 && findQuickSearchMatch(ENTRIES, "HEL", 0) == 2, "prefix matching must ignore case");
    success &= expect(findQuickSearchMatch(ENTRIES, "hel", 3) == 3 && findQuickSearchMatch(ENTRIES, "doc", 5) == 1, "matching must start at the given entry and wrap around");
    success &= expect(findQuickSearchMatch(ENTRIES, "report", 0) == 6 && findQuickSearchMatch(ENTRIES, "XAG", 0) == 4, "a contained query must be found without a prefix match");
    success &= expect(findQuickSearchMatch({{.name = "area.txt"}, {.name = "readme"}}, "re", 0) == 1, "a prefix match must win over an earlier contained one");
    success &= expect(!findQuickSearchMatch(ENTRIES, "zzz", 0) && !findQuickSearchMatch(ENTRIES, "", 0) && !findQuickSearchMatch({}, "a", 0), "nothing must match an absent query");

    using namespace std::chrono_literals;
    const auto   START = CQuickSearch::Clock::time_point{} + 1h;

    CQuickSearch search;
    success &= expect(search.type("h", ENTRIES, std::nullopt, START) == 2 && search.type("e", ENTRIES, 2, START + 100ms) == 2, "a growing query must keep a matching entry");
    success &= expect(search.type("l", ENTRIES, 2, START + 200ms) == 2 && search.type("p", ENTRIES, 2, START + 300ms) == 3, "a growing query must move on once it stops matching");
    success &= expect(search.query() == "help" && search.active(START + 300ms) && !search.active(START + 2s), "the query must expire after the timeout");
    success &= expect(search.backspace(ENTRIES, 3, START + 400ms) == 3 && search.query() == "hel", "backspace must shorten the query");
    success &= expect(search.type("n", ENTRIES, 3, START + 3s) == 5 && search.query() == "n", "typing after the timeout must start a new query");

    search.reset();
    success &= expect(search.type("d", ENTRIES, std::nullopt, START + 4s) == 0, "a new query must start from the current entry");
    success &= expect(search.type("d", ENTRIES, 0, START + 4s) == 1 && search.type("d", ENTRIES, 1, START + 4s) == 0, "repeating a character must cycle through its matches");

    search.reset();
    search.type("é", ENTRIES, std::nullopt, START + 5s);
    success &= expect(!search.backspace(ENTRIES, std::nullopt, START + 5s) && search.query().empty(), "backspace must remove a whole UTF-8 character");

    return success;
}

int main() {
    bool success = true;

    success &= testGlob();
    success &= testMimeDatabase();
    success &= testOptions();
    success &= testOutput();
    success &= testSorting();
    success &= testFormatting();
    success &= testListing();
    success &= testUserDirs();
    success &= testQuickSearch();

    return success ? 0 : 1;
}

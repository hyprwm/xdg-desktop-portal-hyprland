#include "Glob.hpp"
#include "MimeDatabase.hpp"
#include "PickerOptions.hpp"

#include <array>
#include <iostream>

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

int main() {
    bool success = true;

    success &= testGlob();
    success &= testMimeDatabase();
    success &= testOptions();
    success &= testOutput();

    return success ? 0 : 1;
}

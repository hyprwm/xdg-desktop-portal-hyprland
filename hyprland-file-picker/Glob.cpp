#include "Glob.hpp"

#include <string>

static char fold(char c, bool caseSensitive) {
    if (caseSensitive || c < 'A' || c > 'Z')
        return c;

    return c - 'A' + 'a';
}

// Matches c against the class opening at pattern[open]. Returns the index past the closing ']',
// or npos for a malformed class, which is then matched as a literal '['.
static size_t matchClass(std::string_view pattern, size_t open, char c, bool caseSensitive, bool& matched) {
    size_t i      = open + 1;
    bool   negate = false;
    if (i < pattern.size() && (pattern[i] == '!' || pattern[i] == '^')) {
        negate = true;
        ++i;
    }

    const char CHAR  = fold(c, caseSensitive);
    bool       found = false;
    bool       first = true;
    while (i < pattern.size() && (first || pattern[i] != ']')) {
        first = false;

        if (i + 2 < pattern.size() && pattern[i + 1] == '-' && pattern[i + 2] != ']') {
            found = found || (fold(pattern[i], caseSensitive) <= CHAR && CHAR <= fold(pattern[i + 2], caseSensitive));
            i += 3;
            continue;
        }

        found = found || fold(pattern[i], caseSensitive) == CHAR;
        ++i;
    }

    if (i >= pattern.size())
        return std::string_view::npos;

    matched = found != negate;
    return i + 1;
}

bool globMatch(std::string_view pattern, std::string_view text, bool caseSensitive) {
    size_t p = 0, t = 0;

    // where to resume after the last '*' if the rest fails to match
    size_t starP = std::string_view::npos, starT = 0;

    while (t < text.size()) {
        if (p < pattern.size() && pattern[p] == '*') {
            starP = p++;
            starT = t;
            continue;
        }

        if (p < pattern.size()) {
            bool   matched = false;
            size_t next    = std::string_view::npos;

            if (pattern[p] == '?')
                matched = true;
            else if (pattern[p] == '[')
                next = matchClass(pattern, p, text[t], caseSensitive, matched);

            if (next == std::string_view::npos) {
                next    = p + 1;
                matched = matched || fold(pattern[p], caseSensitive) == fold(text[t], caseSensitive);
            }

            if (matched) {
                p = next;
                ++t;
                continue;
            }
        }

        if (starP == std::string_view::npos)
            return false;

        p = starP + 1;
        t = ++starT;
    }

    while (p < pattern.size() && pattern[p] == '*') {
        ++p;
    }

    return p == pattern.size();
}

#include "MimeDatabase.hpp"
#include "Directory.hpp"
#include "Glob.hpp"

#include <algorithm>
#include <cstdlib>
#include <ranges>
#include <unordered_set>

static std::string lowercase(std::string_view text) {
    std::string result{text};
    for (auto& c : result) {
        if (c >= 'A' && c <= 'Z')
            c = c - 'A' + 'a';
    }

    return result;
}

static std::vector<std::string_view> splitLines(std::string_view data) {
    std::vector<std::string_view> lines;
    for (const auto LINE : data | std::views::split('\n')) {
        const std::string_view VIEW{LINE.begin(), LINE.end()};
        if (VIEW.empty() || VIEW.starts_with('#'))
            continue;

        lines.emplace_back(VIEW);
    }

    return lines;
}

static bool hasFlag(std::string_view flags, std::string_view flag) {
    for (const auto FLAG : flags | std::views::split(',')) {
        if (std::string_view{FLAG.begin(), FLAG.end()} == flag)
            return true;
    }

    return false;
}

void CMimeDatabase::load(const std::vector<std::filesystem::path>& dataDirs) {
    for (const auto& dir : dataDirs) {
        const auto MIME_DIR = dir / "mime";

        if (const auto DATA = readTextFile(MIME_DIR / "globs2"); DATA)
            parseGlobs(*DATA);
        if (const auto DATA = readTextFile(MIME_DIR / "aliases"); DATA)
            parseAliases(*DATA);
        if (const auto DATA = readTextFile(MIME_DIR / "subclasses"); DATA)
            parseSubclasses(*DATA);
        if (const auto DATA = readTextFile(MIME_DIR / "generic-icons"); DATA)
            parseGenericIcons(*DATA);
    }
}

void CMimeDatabase::parseGlobs(std::string_view data) {
    for (const auto LINE : splitLines(data)) {
        // weight:type:pattern[:flags]
        std::vector<std::string_view> fields;
        for (const auto FIELD : LINE | std::views::split(':')) {
            fields.emplace_back(FIELD.begin(), FIELD.end());
        }

        if (fields.size() < 3 || fields[2] == "__NOGLOBS__")
            continue;

        const auto TYPE           = lowercase(fields[1]);
        const auto PATTERN        = fields[2];
        const bool CASE_SENSITIVE = fields.size() > 3 && hasFlag(fields[3], "cs");

        // plain "*.ext" globs are by far the most common, look them up by suffix
        if (PATTERN.starts_with("*.") && PATTERN.find_first_of("*?[", 1) == std::string_view::npos) {
            if (CASE_SENSITIVE)
                m_suffixesCaseSensitive.emplace(PATTERN.substr(1), TYPE);
            else
                m_suffixes.emplace(lowercase(PATTERN.substr(1)), TYPE);
            continue;
        }

        m_globs.emplace_back(SGlob{
            .pattern       = std::string{PATTERN},
            .type          = TYPE,
            .caseSensitive = CASE_SENSITIVE,
        });
    }
}

void CMimeDatabase::parseAliases(std::string_view data) {
    for (const auto LINE : splitLines(data)) {
        const auto SPACE = LINE.find(' ');
        if (SPACE == std::string_view::npos)
            continue;

        m_aliases.emplace(lowercase(LINE.substr(0, SPACE)), lowercase(LINE.substr(SPACE + 1)));
    }
}

void CMimeDatabase::parseSubclasses(std::string_view data) {
    for (const auto LINE : splitLines(data)) {
        const auto SPACE = LINE.find(' ');
        if (SPACE == std::string_view::npos)
            continue;

        m_parents[lowercase(LINE.substr(0, SPACE))].emplace_back(lowercase(LINE.substr(SPACE + 1)));
    }
}

void CMimeDatabase::parseGenericIcons(std::string_view data) {
    for (const auto LINE : splitLines(data)) {
        const auto COLON = LINE.find(':');
        if (COLON == std::string_view::npos)
            continue;

        m_genericIcons.emplace(lowercase(LINE.substr(0, COLON)), LINE.substr(COLON + 1));
    }
}

std::string CMimeDatabase::typeForName(std::string_view name) const {
    const auto LOWER = lowercase(name);

    // the longest suffix wins, so ".tar.gz" is checked before ".gz"
    for (size_t dot = name.find('.'); dot != std::string_view::npos; dot = name.find('.', dot + 1)) {
        if (const auto IT = m_suffixesCaseSensitive.find(std::string{name.substr(dot)}); IT != m_suffixesCaseSensitive.end())
            return IT->second;

        if (const auto IT = m_suffixes.find(LOWER.substr(dot)); IT != m_suffixes.end())
            return IT->second;
    }

    for (const auto& glob : m_globs) {
        if (globMatch(glob.pattern, name, glob.caseSensitive))
            return glob.type;
    }

    return "";
}

std::string CMimeDatabase::canonical(std::string_view type) const {
    auto lower = lowercase(type);
    if (const auto IT = m_aliases.find(lower); IT != m_aliases.end())
        return IT->second;

    return lower;
}

bool CMimeDatabase::isA(std::string_view type, std::string_view parent) const {
    const auto PARENT = canonical(parent);
    if (PARENT == "application/octet-stream" || PARENT == "*/*" || PARENT == "*")
        return true;

    const auto TYPE = canonical(type);
    if (TYPE.empty())
        return false;

    if (PARENT.ends_with("/*"))
        return TYPE.starts_with(std::string_view{PARENT}.substr(0, PARENT.size() - 1));

    std::vector<std::string>        pending = {TYPE};
    std::unordered_set<std::string> seen;
    while (!pending.empty()) {
        const auto CURRENT = std::move(pending.back());
        pending.pop_back();

        if (!seen.insert(CURRENT).second)
            continue;

        // per the spec, every text type is a text/plain
        if (CURRENT == PARENT || (PARENT == "text/plain" && CURRENT.starts_with("text/")))
            return true;

        const auto IT = m_parents.find(CURRENT);
        if (IT == m_parents.end())
            continue;

        for (const auto& next : IT->second) {
            pending.emplace_back(canonical(next));
        }
    }

    return false;
}

std::vector<std::string> CMimeDatabase::iconsForType(std::string_view type) const {
    std::vector<std::string> icons;

    if (const auto TYPE = canonical(type); !TYPE.empty()) {
        auto specific = TYPE;
        std::ranges::replace(specific, '/', '-');
        icons.emplace_back(std::move(specific));

        if (const auto IT = m_genericIcons.find(TYPE); IT != m_genericIcons.end())
            icons.emplace_back(IT->second);
        else
            icons.emplace_back(TYPE.substr(0, TYPE.find('/')) + "-x-generic");
    }

    if (!std::ranges::contains(icons, "text-x-generic"))
        icons.emplace_back("text-x-generic");

    return icons;
}

std::vector<std::filesystem::path> xdgDataDirs() {
    std::vector<std::filesystem::path> dirs;

    if (const auto DATA_HOME = std::getenv("XDG_DATA_HOME"); DATA_HOME && *DATA_HOME)
        dirs.emplace_back(DATA_HOME);
    else if (const auto HOME = std::getenv("HOME"); HOME && *HOME)
        dirs.emplace_back(std::filesystem::path{HOME} / ".local" / "share");

    const auto             DATA_DIRS = std::getenv("XDG_DATA_DIRS");
    const std::string_view LIST      = DATA_DIRS && *DATA_DIRS ? DATA_DIRS : "/usr/local/share:/usr/share";
    for (const auto DIR : LIST | std::views::split(':')) {
        if (!DIR.empty())
            dirs.emplace_back(std::string_view{DIR.begin(), DIR.end()});
    }

    return dirs;
}

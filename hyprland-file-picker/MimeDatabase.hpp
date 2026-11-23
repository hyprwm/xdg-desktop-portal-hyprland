#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// A name-based view of the shared-mime-info database. No content sniffing.
class CMimeDatabase {
  public:
    // loads <dir>/mime/{globs2,aliases,subclasses,generic-icons}, most important dir first
    void load(const std::vector<std::filesystem::path>& dataDirs);

    void parseGlobs(std::string_view data);
    void parseAliases(std::string_view data);
    void parseSubclasses(std::string_view data);
    void parseGenericIcons(std::string_view data);

    // empty if no pattern matches the name
    std::string typeForName(std::string_view name) const;

    // alias and subclass aware. "image/*" matches every image type, and every type,
    // including an unknown (empty) one, is an application/octet-stream.
    bool isA(std::string_view type, std::string_view parent) const;

    // icon names to try, most specific first
    std::vector<std::string> iconsForType(std::string_view type) const;

  private:
    struct SGlob {
        std::string pattern;
        std::string type;
        bool        caseSensitive = false;
    };

    std::string canonical(std::string_view type) const;

    // "*.ext" globs, keyed by ".ext". The case-insensitive ones are keyed lowercase.
    std::unordered_map<std::string, std::string> m_suffixes;
    std::unordered_map<std::string, std::string> m_suffixesCaseSensitive;

    // everything else, by descending weight
    std::vector<SGlob>                                        m_globs;

    std::unordered_map<std::string, std::string>              m_aliases;
    std::unordered_map<std::string, std::vector<std::string>> m_parents;
    std::unordered_map<std::string, std::string>              m_genericIcons;
};

struct SFilter;

// globs match case-insensitively, mime types through CMimeDatabase::isA. A filter without rules matches everything.
bool matchesFilter(const SFilter& filter, std::string_view name, const CMimeDatabase& mime);

// $XDG_DATA_HOME followed by $XDG_DATA_DIRS, with the spec's defaults
std::vector<std::filesystem::path> xdgDataDirs();

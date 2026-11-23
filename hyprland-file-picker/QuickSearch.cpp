#include "QuickSearch.hpp"

#include <hyprutils/memory/Casts.hpp>

#include <algorithm>

static char fold(char c) {
    if (c >= 'A' && c <= 'Z')
        return c - 'A' + 'a';

    return c;
}

static bool equalFolded(char a, char b) {
    return fold(a) == fold(b);
}

static bool startsWithFolded(std::string_view name, std::string_view query) {
    return name.size() >= query.size() && std::ranges::equal(name.substr(0, query.size()), query, equalFolded);
}

static bool containsFolded(std::string_view name, std::string_view query) {
    return !std::ranges::search(name, query, equalFolded).empty();
}

// the query is the same character typed over and over, e.g. "fff"
static bool isRepeated(std::string_view query, std::string_view character) {
    if (character.empty() || query.size() <= character.size() || query.size() % character.size() != 0)
        return false;

    for (size_t i = 0; i < query.size(); i += character.size()) {
        if (query.substr(i, character.size()) != character)
            return false;
    }

    return true;
}

std::optional<size_t> CQuickSearch::type(std::string_view text, const std::vector<SFileEntry>& entries, std::optional<size_t> current, Clock::time_point now) {
    if (now - m_lastInput > TIMEOUT)
        m_query.clear();

    m_lastInput = now;
    m_query += text;

    // typing the same character again moves on to the next entry starting with it
    if (isRepeated(m_query, text))
        return findQuickSearchMatch(entries, text, current ? *current + 1 : 0);

    // the current entry stays selected while it keeps matching a growing query
    return findQuickSearchMatch(entries, m_query, current.value_or(0));
}

std::optional<size_t> CQuickSearch::backspace(const std::vector<SFileEntry>& entries, std::optional<size_t> current, Clock::time_point now) {
    // a whole UTF-8 character: its continuation bytes, then the lead byte
    while (!m_query.empty() && (Hyprutils::Memory::sc<unsigned char>(m_query.back()) & 0xC0) == 0x80) {
        m_query.pop_back();
    }

    if (!m_query.empty())
        m_query.pop_back();

    m_lastInput = now;
    return findQuickSearchMatch(entries, m_query, current.value_or(0));
}

void CQuickSearch::reset() {
    m_query.clear();
}

bool CQuickSearch::active(Clock::time_point now) const {
    return !m_query.empty() && now - m_lastInput <= TIMEOUT;
}

const std::string& CQuickSearch::query() const {
    return m_query;
}

std::optional<size_t> findQuickSearchMatch(const std::vector<SFileEntry>& entries, std::string_view query, size_t start) {
    if (entries.empty() || query.empty())
        return std::nullopt;

    // a name starting with the query beats one that merely contains it
    for (const auto MATCHES : {startsWithFolded, containsFolded}) {
        for (size_t i = 0; i < entries.size(); ++i) {
            const auto INDEX = (start + i) % entries.size();
            if (MATCHES(entries[INDEX].name, query))
                return INDEX;
        }
    }

    return std::nullopt;
}

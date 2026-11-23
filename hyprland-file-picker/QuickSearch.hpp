#pragma once

#include "Directory.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Dolphin style type-ahead: typing builds a query that jumps to the first matching entry
class CQuickSearch {
  public:
    using Clock = std::chrono::steady_clock;

    // after this long without typing, the next character starts a new query
    static constexpr auto TIMEOUT = std::chrono::seconds(1);

    // returns the entry to select, if any matches
    std::optional<size_t> type(std::string_view text, const std::vector<SFileEntry>& entries, std::optional<size_t> current, Clock::time_point now);
    std::optional<size_t> backspace(const std::vector<SFileEntry>& entries, std::optional<size_t> current, Clock::time_point now);
    void                  reset();

    bool                  active(Clock::time_point now) const;
    const std::string&    query() const;

  private:
    std::string       m_query;
    Clock::time_point m_lastInput;
};

// the first entry from start on, wrapping around, whose name starts with the query, otherwise the first one containing it.
// Case-insensitive for ASCII.
std::optional<size_t> findQuickSearchMatch(const std::vector<SFileEntry>& entries, std::string_view query, size_t start);

#pragma once

#include <string_view>

// Shell-style glob matching with *, ? and [...] classes (ranges, ! or ^ negation).
// Works on bytes, case folding is ASCII only.
bool globMatch(std::string_view pattern, std::string_view text, bool caseSensitive = true);

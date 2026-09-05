#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct SWindowEntry {
    uint32_t    id = 0;
    std::string windowClass;
    std::string title;
};

struct SOutputEntry {
    std::string name;
    std::string description;
    int32_t     x           = 0;
    int32_t     y           = 0;
    int32_t     width       = 0;
    int32_t     height      = 0;
    bool        hasGeometry = false;
};

enum eSelectionType : uint8_t {
    SELECTION_NONE = 0,
    SELECTION_OUTPUT,
    SELECTION_WINDOW,
    SELECTION_REGION,
};

struct SSelection {
    eSelectionType type = SELECTION_NONE;
    std::string    output;
    uint32_t       windowID = 0;
    int32_t        x        = 0;
    int32_t        y        = 0;
    int32_t        width    = 0;
    int32_t        height   = 0;
};

std::vector<SWindowEntry> parseWindowList(std::string_view list);
std::vector<SOutputEntry> parseOutputList(std::string_view list);
std::optional<SSelection> parseRegion(std::string_view result, const std::vector<SOutputEntry>& outputs);
std::string               formatSelection(const SSelection& selection, bool allowToken);
std::string               escapeMarkup(std::string_view text);

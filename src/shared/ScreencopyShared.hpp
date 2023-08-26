#pragma once

#include <string>
#include <cstdint>

enum eSelectionType
{
    TYPE_INVALID = -1,
    TYPE_OUTPUT  = 0,
    TYPE_WINDOW,
    TYPE_GEOMETRY,
    TYPE_WORKSPACE,
};

struct SSelectionData {
    eSelectionType type = TYPE_INVALID;
    std::string    output;
    uint64_t       windowHandle = 0;
    uint32_t       x = 0, y = 0, w = 0, h = 0; // for TYPE_GEOMETRY
};

SSelectionData promptForScreencopySelection();
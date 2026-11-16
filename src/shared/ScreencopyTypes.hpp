#pragma once

enum cursorModes {
    HIDDEN   = 1,
    EMBEDDED = 2,
    METADATA = 4,
};

enum eSelectionType {
    TYPE_INVALID = -1,
    TYPE_OUTPUT  = 0,
    TYPE_WINDOW,
    TYPE_GEOMETRY,
    TYPE_WORKSPACE,
};

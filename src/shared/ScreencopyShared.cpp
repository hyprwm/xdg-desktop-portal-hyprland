#include "ScreencopyShared.hpp"
#include "../helpers/MiscFunctions.hpp"

SSelectionData promptForScreencopySelection() {
    SSelectionData data;

    const auto     RETVAL = execAndGet("hyprland-share-picker");

    if (RETVAL.find("screen:") == 0) {
        data.type   = TYPE_OUTPUT;
        data.output = RETVAL.substr(7);
    } else if (RETVAL.find("window:") == 0) {
        // todo
    } else if (RETVAL.find("region:") == 0) {
        std::string running = RETVAL;
        running             = running.substr(7);
        data.type           = TYPE_GEOMETRY;
        data.output         = running.substr(0, running.find_first_of('@'));
        running             = running.substr(running.find_first_of('@') + 1);

        data.x  = std::stoi(running.substr(running.find_first_of(',')));
        running = running.substr(running.find_first_of(',') + 1);
        data.y  = std::stoi(running.substr(running.find_first_of(',')));
        running = running.substr(running.find_first_of(',') + 1);
        data.w  = std::stoi(running.substr(running.find_first_of(',')));
        running = running.substr(running.find_first_of(',') + 1);
        data.h  = std::stoi(running);
    }

    return data;
}
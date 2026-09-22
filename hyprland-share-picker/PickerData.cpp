#include "PickerData.hpp"

#include <algorithm>
#include <array>
#include <charconv>

static bool parseUint32(std::string_view value, uint32_t& result) {
    if (value.empty())
        return false;

    const auto PARSED = std::from_chars(value.data(), value.data() + value.size(), result);
    return PARSED.ec == std::errc{} && PARSED.ptr == value.data() + value.size();
}

static bool parseInt32(std::string_view value, int32_t& result) {
    if (value.empty())
        return false;

    const auto PARSED = std::from_chars(value.data(), value.data() + value.size(), result);
    return PARSED.ec == std::errc{} && PARSED.ptr == value.data() + value.size();
}

static std::optional<std::string_view> takeUntil(std::string_view& value, std::string_view separator) {
    const auto POS = value.find(separator);
    if (POS == std::string_view::npos)
        return std::nullopt;

    const auto RESULT = value.substr(0, POS);
    value.remove_prefix(POS + separator.size());
    return RESULT;
}

std::vector<SWindowEntry> parseWindowList(std::string_view list) {
    std::vector<SWindowEntry> result;

    while (!list.empty()) {
        const auto ID    = takeUntil(list, "[HC>]");
        const auto CLASS = takeUntil(list, "[HT>]");
        const auto TITLE = takeUntil(list, "[HE>]");
        const auto ADDR  = takeUntil(list, "[HA>]");

        if (!ID || !CLASS || !TITLE || !ADDR)
            break;

        uint32_t parsedID = 0;
        if (!parseUint32(*ID, parsedID))
            continue;

        result.emplace_back(SWindowEntry{
            .id          = parsedID,
            .windowClass = std::string{*CLASS},
            .title       = std::string{*TITLE},
        });
    }

    return result;
}

std::vector<SOutputEntry> parseOutputList(std::string_view list) {
    std::vector<SOutputEntry> result;

    while (!list.empty()) {
        const auto LENGTH_END = list.find(':');
        if (LENGTH_END == std::string_view::npos)
            break;

        uint32_t nameLength = 0;
        if (!parseUint32(list.substr(0, LENGTH_END), nameLength))
            break;

        list.remove_prefix(LENGTH_END + 1);
        if (nameLength > list.size())
            break;

        SOutputEntry output{
            .name        = std::string{list.substr(0, nameLength)},
            .description = "",
        };
        list.remove_prefix(nameLength);

        if (list.empty() || list.front() != ':')
            break;
        list.remove_prefix(1);

        const auto X      = takeUntil(list, ":");
        const auto Y      = takeUntil(list, ":");
        const auto WIDTH  = takeUntil(list, ":");
        const auto HEIGHT = takeUntil(list, ";");
        if (!X || !Y || !WIDTH || !HEIGHT)
            break;

        if (!parseInt32(*X, output.x) || !parseInt32(*Y, output.y) || !parseInt32(*WIDTH, output.width) || !parseInt32(*HEIGHT, output.height) || output.width <= 0 ||
            output.height <= 0)
            continue;

        output.hasGeometry = true;
        result.emplace_back(std::move(output));
    }

    return result;
}

std::optional<SSelection> parseRegion(std::string_view result, const std::vector<SOutputEntry>& outputs) {
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
        result.remove_suffix(1);

    std::array<std::string_view, 5> fields;
    for (size_t i = 0; i < fields.size() - 1; ++i) {
        const auto VALUE = takeUntil(result, " ");
        if (!VALUE || VALUE->empty())
            return std::nullopt;
        fields[i] = *VALUE;
    }
    fields.back() = result;

    if (fields.back().empty() || fields.back().contains(' '))
        return std::nullopt;

    int32_t x = 0, y = 0, width = 0, height = 0;
    if (!parseInt32(fields[1], x) || !parseInt32(fields[2], y) || !parseInt32(fields[3], width) || !parseInt32(fields[4], height) || x < 0 || y < 0 || width <= 0 || height <= 0)
        return std::nullopt;

    const auto OUTPUT = std::ranges::find_if(outputs, [&fields](const auto& output) { return output.name == fields[0]; });
    if (OUTPUT == outputs.end())
        return std::nullopt;

    return SSelection{
        .type   = SELECTION_REGION,
        .output = OUTPUT->name,
        .x      = x,
        .y      = y,
        .width  = width,
        .height = height,
    };
}

std::string formatSelection(const SSelection& selection, bool allowToken) {
    std::string result = std::string{"[SELECTION]"} + (allowToken ? "r/" : "/");

    switch (selection.type) {
        case SELECTION_OUTPUT: return result + "screen:" + selection.output + "\n";
        case SELECTION_WINDOW: return result + "window:" + std::to_string(selection.windowID) + "\n";
        case SELECTION_REGION:
            return result + "region:" + selection.output + "@" + std::to_string(selection.x) + "," + std::to_string(selection.y) + "," + std::to_string(selection.width) + "," +
                std::to_string(selection.height) + "\n";
        case SELECTION_NONE: return {};
    }

    return {};
}

std::string escapeMarkup(std::string_view text) {
    std::string result;
    result.reserve(text.size());

    for (const auto CHARACTER : text) {
        if (static_cast<unsigned char>(CHARACTER) < 0x20) {
            result += ' ';
            continue;
        }

        switch (CHARACTER) {
            case '&': result += "&amp;"; break;
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '\'': result += "&apos;"; break;
            case '"': result += "&quot;"; break;
            default: result += CHARACTER; break;
        }
    }

    return result;
}

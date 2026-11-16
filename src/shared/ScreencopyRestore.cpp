#include "ScreencopyRestore.hpp"
#include "RestoreTokenStore.hpp"

#include <algorithm>

using TRestoreEnvelope                      = sdbus::Struct<std::string, uint32_t, sdbus::Variant>;
static constexpr uint32_t   RESTORE_VERSION = 4;

std::optional<SRestoreData> ScreencopyRestore::consume(CRestoreTokenStore& store, const sdbus::Variant& restoreData, std::string_view appID, uint32_t requestedMode) {
    if (requestedMode > 2)
        return std::nullopt;

    try {
        const auto ENVELOPE = restoreData.get<TRestoreEnvelope>();
        if (ENVELOPE.get<0>() != "hyprland" || ENVELOPE.get<1>() != RESTORE_VERSION)
            return std::nullopt;

        auto data = store.consume(ENVELOPE.get<2>().get<std::string>(), appID);
        if (data)
            data->persistMode = std::min(requestedMode, data->persistMode);
        return data;
    } catch (const sdbus::Error&) {
        // Old, foreign or malformed private data must fall back to the picker.
        return std::nullopt;
    }
}

std::unordered_map<std::string, sdbus::Variant> ScreencopyRestore::issue(CRestoreTokenStore& store, const SRestoreData& data, bool allowToken) {
    std::unordered_map<std::string, sdbus::Variant> options = {
        {"persist_mode", sdbus::Variant{uint32_t{0}}},
    };
    if (!allowToken || data.persistMode == 0)
        return options;

    const auto TOKEN = store.issue(data);
    if (!TOKEN)
        return options;

    options["restore_data"] = sdbus::Variant{TRestoreEnvelope{"hyprland", RESTORE_VERSION, sdbus::Variant{*TOKEN}}};
    options["persist_mode"] = sdbus::Variant{data.persistMode};
    return options;
}

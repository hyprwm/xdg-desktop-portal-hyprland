#pragma once

#include <sdbus-c++/Types.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

class CRestoreTokenStore;
struct SRestoreData;

namespace ScreencopyRestore {
    std::optional<SRestoreData>                     consume(CRestoreTokenStore& store, const sdbus::Variant& restoreData, std::string_view appID, uint32_t requestedMode);
    std::unordered_map<std::string, sdbus::Variant> issue(CRestoreTokenStore& store, const SRestoreData& data, bool allowToken);
}

#pragma once

#include "ScreencopyTypes.hpp"

#include <hyprutils/memory/UniquePtr.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Hyprutils::State {
    class CKVStateFile;
}

struct SRestoreData {
    std::string    appID;
    eSelectionType type = TYPE_INVALID;
    std::string    output, windowClass, workspace;
    uint32_t       x = 0, y = 0, w = 0, h = 0;
    uint32_t       cursorMode  = HIDDEN;
    uint32_t       persistMode = 0;
};

// Used only on the portal event-loop thread. Each token authorizes one restore.
class CRestoreTokenStore {
  public:
    CRestoreTokenStore();
    explicit CRestoreTokenStore(const std::optional<std::filesystem::path>& path);
    ~CRestoreTokenStore();

    bool                        persistentAvailable() const;
    std::optional<std::string>  issue(const SRestoreData& data);
    std::optional<SRestoreData> consume(const std::string& token, std::string_view appID);

  private:
    struct STransientGrant {
        SRestoreData                          data;
        std::chrono::steady_clock::time_point issued;
    };

    Hyprutils::Memory::CUniquePointer<Hyprutils::State::CKVStateFile> m_persistent;
    std::unordered_map<std::string, STransientGrant>                  m_transient;
};

#include "RestoreTokenStore.hpp"
#include "../helpers/UUID.hpp"

#include <hyprutils/memory/Casts.hpp>
#include <hyprutils/path/Path.hpp>
#include <hyprutils/state/KVStateFile.hpp>

#include <algorithm>
#include <charconv>
#include <limits>

using namespace Hyprutils::Memory;
using namespace Hyprutils::State;

static constexpr size_t MAX_TOKENS = 1024;

static bool             validData(const SRestoreData& data) {
    if ((data.persistMode != 1 && data.persistMode != 2) || (data.cursorMode != HIDDEN && data.cursorMode != EMBEDDED))
        return false;

    switch (data.type) {
        case TYPE_OUTPUT: return !data.output.empty();
        case TYPE_WINDOW: return !data.windowClass.empty();
        case TYPE_WORKSPACE: return !data.workspace.empty();
        case TYPE_GEOMETRY: {
            // capture_output_region takes signed 32-bit coordinates and dimensions.
            constexpr auto MAX = sc<uint32_t>(std::numeric_limits<int32_t>::max());
            return !data.output.empty() && data.w > 0 && data.h > 0 && data.x <= MAX && data.y <= MAX && data.w <= MAX && data.h <= MAX;
        }
        default: return false;
    }
}

static std::unordered_map<std::string, std::string> encode(const SRestoreData& data) {
    std::unordered_map<std::string, std::string> values = {
        {"version", "1"},
        {"app_id", data.appID},
        {"type", std::to_string(data.type)},
        {"cursor_mode", std::to_string(data.cursorMode)},
        {"persist_mode", std::to_string(data.persistMode)},
    };

    switch (data.type) {
        case TYPE_GEOMETRY:
            values["x"] = std::to_string(data.x);
            values["y"] = std::to_string(data.y);
            values["w"] = std::to_string(data.w);
            values["h"] = std::to_string(data.h);
            [[fallthrough]];
        case TYPE_OUTPUT: values["output"] = data.output; break;
        case TYPE_WINDOW: values["window_class"] = data.windowClass; break;
        case TYPE_WORKSPACE: values["workspace"] = data.workspace; break;
        default: break;
    }
    return values;
}

static bool readUint(const CKVStateEntry& entry, std::string_view name, uint32_t& value) {
    const auto TEXT = entry.get(name);
    if (!TEXT || TEXT->empty())
        return false;
    const auto RESULT = std::from_chars(TEXT->data(), TEXT->data() + TEXT->size(), value);
    return RESULT.ec == std::errc{} && RESULT.ptr == TEXT->data() + TEXT->size();
}

static std::optional<SRestoreData> decode(const CKVStateEntry& entry) {
    const auto VERSION = entry.get("version");
    const auto APPID   = entry.get("app_id");
    if (!VERSION || *VERSION != "1" || !APPID)
        return std::nullopt;

    SRestoreData data;
    uint32_t     type = 0;
    if (!readUint(entry, "type", type) || type > TYPE_WORKSPACE || !readUint(entry, "cursor_mode", data.cursorMode) || !readUint(entry, "persist_mode", data.persistMode) ||
        data.persistMode != 2)
        return std::nullopt;

    data.appID       = *APPID;
    data.type        = sc<eSelectionType>(type);
    data.output      = entry.get("output").value_or("");
    data.windowClass = entry.get("window_class").value_or("");
    data.workspace   = entry.get("workspace").value_or("");
    if (data.type == TYPE_GEOMETRY && (!readUint(entry, "x", data.x) || !readUint(entry, "y", data.y) || !readUint(entry, "w", data.w) || !readUint(entry, "h", data.h)))
        return std::nullopt;

    if (!validData(data))
        return std::nullopt;
    return data;
}

CRestoreTokenStore::CRestoreTokenStore() :
    CRestoreTokenStore(Hyprutils::Path::getDataDir("xdg-desktop-portal-hyprland").transform([](const auto& dir) { return std::filesystem::path(dir) / "screencopy-tokens"; })) {
    ;
}

CRestoreTokenStore::CRestoreTokenStore(const std::optional<std::filesystem::path>& path) {
    if (!path || !path->is_absolute())
        return;

    std::error_code error;
    std::filesystem::create_directories(path->parent_path(), error);
    if (error)
        return;
    std::filesystem::permissions(path->parent_path(), std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, error);
    if (error)
        return;

    m_persistent = makeUnique<CKVStateFile>(*path);
    m_persistent->pruneOld(MAX_TOKENS);
}

CRestoreTokenStore::~CRestoreTokenStore() = default;

bool CRestoreTokenStore::persistentAvailable() const {
    return m_persistent && m_persistent->good();
}

std::optional<std::string> CRestoreTokenStore::issue(const SRestoreData& data) {
    if (!validData(data) || (data.persistMode == 2 && !persistentAvailable()))
        return std::nullopt;

    auto token = getRandomUUID();
    while (m_transient.contains(token) || (m_persistent && m_persistent->get(token))) {
        token = getRandomUUID();
    }

    if (data.persistMode == 1) {
        if (m_transient.size() >= MAX_TOKENS) {
            const auto OLDEST = std::ranges::min_element(m_transient, {}, [](const auto& entry) { return entry.second.issued; });
            m_transient.erase(OLDEST);
        }
        m_transient.emplace(token, STransientGrant{data, std::chrono::steady_clock::now()});
        return token;
    }

    auto values = encode(data);
    m_persistent->pruneOld(MAX_TOKENS - 1);
    if (!m_persistent->good() || !m_persistent->insert(token, std::move(values)))
        return std::nullopt;
    return token;
}

std::optional<SRestoreData> CRestoreTokenStore::consume(const std::string& token, std::string_view appID) {
    if (token.size() != 36)
        return std::nullopt;

    const auto TRANSIENT = m_transient.find(token);
    if (TRANSIENT != m_transient.end()) {
        if (TRANSIENT->second.data.appID != appID)
            return std::nullopt;
        auto data = TRANSIENT->second.data;
        m_transient.erase(TRANSIENT);
        return data;
    }

    if (!persistentAvailable())
        return std::nullopt;
    const auto ENTRY = m_persistent->get(token);
    if (!ENTRY)
        return std::nullopt;
    auto data = decode(*ENTRY);
    if (!data || data->appID != appID || !m_persistent->remove(token))
        return std::nullopt;
    return data;
}

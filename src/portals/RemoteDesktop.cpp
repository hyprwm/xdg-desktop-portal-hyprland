#include "RemoteDesktop.hpp"
#include "../core/PortalManager.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <format>
#include <libeis.h>
#include <linux/input.h>
#include <sstream>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>

// Helper: get current time in ms for Wayland events
static uint32_t currentTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// The impl-side portal interface exchanges persistence state as `restore_data` (suv):
// vendor, version, private blob. xdg-desktop-portal is what turns that into the
// `restore_token` the application sees. Keep the vendor/version in sync with the
// screencopy portal: a combined RemoteDesktop+ScreenCast session shares one blob.
using SRestoreData = sdbus::Struct<std::string, uint32_t, sdbus::Variant>;

static constexpr const char* RESTORE_DATA_VENDOR    = "hyprland";
static constexpr uint32_t    RESTORE_DATA_VERSION   = 3;
static constexpr const char* RESTORE_DATA_TOKEN_KEY = "remoteDesktopToken";

static std::filesystem::path restoreTokenFile() {
    std::filesystem::path base;
    if (const char* STATEHOME = std::getenv("XDG_STATE_HOME"); STATEHOME && *STATEHOME)
        base = std::filesystem::path{STATEHOME};
    else if (const char* HOME = std::getenv("HOME"); HOME && *HOME)
        base = std::filesystem::path{HOME} / ".local/state";
    else
        return {}; // no home: never fall back to a relative path, we'd drop secrets in the cwd

    return base / "xdg-desktop-portal-hyprland" / "remote-desktop-tokens";
}

static std::string newRestoreToken() {
    std::array<unsigned char, 16> bytes;
    size_t                        offset = 0;
    while (offset < bytes.size()) {
        const auto count = getrandom(bytes.data() + offset, bytes.size() - offset, 0);
        if (count < 0) {
            if (errno == EINTR)
                continue;
            return {};
        }
        offset += sc<size_t>(count);
    }

    constexpr char HEX[] = "0123456789abcdef";
    std::string    token;
    token.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        token.push_back(HEX[byte >> 4]);
        token.push_back(HEX[byte & 0xf]);
    }
    return token;
}

static bool readTokenStore(const std::filesystem::path& path, std::string& contents) {
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return false;

    struct stat info;
    if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != getuid()) {
        close(fd);
        return false;
    }

    std::array<char, 4096> buffer;
    while (true) {
        const auto count = read(fd, buffer.data(), buffer.size());
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0) {
            close(fd);
            return false;
        }
        if (count == 0)
            break;
        contents.append(buffer.data(), sc<size_t>(count));
    }
    return close(fd) == 0;
}

static bool writeAll(int fd, const std::string& contents) {
    size_t offset = 0;
    while (offset < contents.size()) {
        const auto count = write(fd, contents.data() + offset, contents.size() - offset);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return false;
        offset += sc<size_t>(count);
    }
    return true;
}

// Checking a token must not spend it: Start can still fail after this (no compositor
// protocols, keymap failure, screencast refused), and burning the user's grant on a
// failed attempt would silently drop them back to the consent dialog next time.
static bool validateRestoreToken(const std::string& token, const std::string& appID, uint32_t deviceTypes) {
    if (token.empty())
        return false;

    const auto path = restoreTokenFile();
    if (path.empty())
        return false;

    std::string contents;
    if (!readTokenStore(path, contents))
        return false;

    std::istringstream input(contents);
    std::string        line;
    while (std::getline(input, line)) {
        std::istringstream fields(line);
        std::string        storedToken, storedAppID, storedDevices;
        if (!std::getline(fields, storedToken, '\t') || !std::getline(fields, storedAppID, '\t') || !std::getline(fields, storedDevices))
            continue;
        if (storedToken == token && storedAppID == appID && storedDevices == std::to_string(deviceTypes))
            return true;
    }

    return false;
}

// Drops the grant, e.g. because the app restored it while asking for no further
// persistence. Without this such an entry would sit in the store forever.
static bool revokeRestoreToken(const std::string& token) {
    if (token.empty())
        return false;

    const auto path = restoreTokenFile();
    if (path.empty())
        return false;

    std::string contents;
    if (!readTokenStore(path, contents))
        return false;

    std::istringstream input(contents);
    std::string        remaining;
    std::string        line;
    bool               matched = false;
    while (std::getline(input, line)) {
        std::istringstream fields(line);
        std::string        storedToken;
        if (!std::getline(fields, storedToken, '\t'))
            continue;
        if (storedToken == token) {
            matched = true;
            continue;
        }
        remaining += line + '\n';
    }

    if (!matched)
        return false;

    // A non-unique suffix would let one leftover .tmp- file (crash between open and
    // rename) block every future revocation, O_EXCL and all.
    const auto suffix = newRestoreToken();
    if (suffix.empty())
        return false;

    const auto temporary = path.string() + ".tmp-" + suffix;
    const int  fd        = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0)
        return false;

    const bool flushed = writeAll(fd, remaining) && fsync(fd) == 0;
    const bool closed  = close(fd) == 0;
    if (!flushed || !closed) {
        unlink(temporary.c_str());
        return false;
    }
    if (rename(temporary.c_str(), path.c_str()) != 0) {
        unlink(temporary.c_str());
        return false;
    }

    const int directory = open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory < 0)
        return false;
    const bool durable = fsync(directory) == 0;
    close(directory);
    return durable;
}

static std::string issueRestoreToken(const std::string& appID, uint32_t deviceTypes) {
    if (appID.find_first_of("\t\n") != std::string::npos) {
        Debug::log(WARN, "[remotedesktop] refusing to persist a token for an app id containing record separators");
        return {};
    }

    const auto token = newRestoreToken();
    if (token.empty())
        return {};

    const auto path = restoreTokenFile();
    if (path.empty())
        return {};

    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error || chmod(path.parent_path().c_str(), 0700) != 0)
        return {};

    const int fd = open(path.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0)
        return {};

    struct stat info;
    if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != getuid() || fchmod(fd, 0600) != 0) {
        close(fd);
        return {};
    }

    const auto record  = std::format("{}\t{}\t{}\n", token, appID, deviceTypes);
    size_t     offset  = 0;
    bool       written = true;
    while (offset < record.size()) {
        const auto count = write(fd, record.data() + offset, record.size() - offset);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0) {
            written = false;
            break;
        }
        offset += sc<size_t>(count);
    }

    // A half-written record has no trailing newline, so the next append would glue
    // itself onto it and take that token down too. Roll back to what we found.
    if (!written || fsync(fd) != 0) {
        if (ftruncate(fd, info.st_size) != 0)
            Debug::log(WARN, "[remotedesktop] could not roll back a partial token record");
        close(fd);
        return {};
    }

    if (close(fd) != 0)
        return {};

    // fsync on the file does not make a freshly created file's directory entry
    // durable, and the revoke path already pays for this.
    const int directory = open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory >= 0) {
        if (fsync(directory) != 0)
            Debug::log(WARN, "[remotedesktop] could not flush the token store directory");
        close(directory);
    }

    return token;
}

static void sendModifiers(CCZwpVirtualKeyboardV1* keyboard, xkb_state* state, xkb_mod_mask_t extraDepressed = 0, xkb_layout_index_t layout = XKB_LAYOUT_INVALID) {
    if (!keyboard || !state)
        return;

    const auto LAYOUT = layout == XKB_LAYOUT_INVALID ? xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_EFFECTIVE) : layout;
    keyboard->sendModifiers(xkb_state_serialize_mods(state, XKB_STATE_MODS_DEPRESSED) | extraDepressed, xkb_state_serialize_mods(state, XKB_STATE_MODS_LATCHED),
                            xkb_state_serialize_mods(state, XKB_STATE_MODS_LOCKED), LAYOUT);
}

static xkb_mod_mask_t activeKeysymModifiers(const std::unordered_map<int32_t, xkb_mod_mask_t>& keysymModifiers) {
    xkb_mod_mask_t modifiers = 0;
    for (const auto& [_, mask] : keysymModifiers)
        modifiers |= mask;
    return modifiers;
}

// ─── CRemoteDesktopPortal implementation ─────────────────────────

CRemoteDesktopPortal::CRemoteDesktopPortal(SP<CCZwlrVirtualPointerManagerV1> pointerMgr, SP<CCZwpVirtualKeyboardManagerV1> keyboardMgr) {
    m_sState.pointer  = pointerMgr;
    m_sState.keyboard = keyboardMgr;

    // Initialize xkbcommon for keysym → keycode conversion
    m_xkbCtx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (m_xkbCtx)
        m_xkbKeymap = xkb_keymap_new_from_names(m_xkbCtx, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);

    m_pObject = sdbus::createObject(*g_pPortalManager->getConnection(), OBJECT_PATH);

    m_pObject
        ->addVTable(
            sdbus::registerMethod("CreateSession")
                .implementedAs(
                    [this](sdbus::ObjectPath o1, sdbus::ObjectPath o2, std::string s, std::unordered_map<std::string, sdbus::Variant> m) { return onCreateSession(o1, o2, s, m); }),
            sdbus::registerMethod("SelectDevices")
                .implementedAs(
                    [this](sdbus::ObjectPath o1, sdbus::ObjectPath o2, std::string s, std::unordered_map<std::string, sdbus::Variant> m) { return onSelectDevices(o1, o2, s, m); }),
            sdbus::registerMethod("Start").implementedAs([this](sdbus::ObjectPath o1, sdbus::ObjectPath o2, std::string s1, std::string s2,
                                                                std::unordered_map<std::string, sdbus::Variant> m) { return onStart(o1, o2, s1, s2, m); }),
            sdbus::registerMethod("ConnectToEIS").implementedAs([this](sdbus::ObjectPath o, std::string s, std::unordered_map<std::string, sdbus::Variant> m) {
                return onConnectToEIS(o, s, m);
            }),
            sdbus::registerMethod("NotifyPointerMotion").implementedAs([this](sdbus::ObjectPath o, std::unordered_map<std::string, sdbus::Variant> m, double d1, double d2) {
                onNotifyPointerMotion(o, m, d1, d2);
            }),
            sdbus::registerMethod("NotifyPointerMotionAbsolute")
                .implementedAs([this](sdbus::ObjectPath o, std::unordered_map<std::string, sdbus::Variant> m, uint32_t u1, double d1, double d2) {
                    onNotifyPointerMotionAbsolute(o, m, u1, d1, d2);
                }),
            sdbus::registerMethod("NotifyPointerButton").implementedAs([this](sdbus::ObjectPath o, std::unordered_map<std::string, sdbus::Variant> m, int32_t i1, uint32_t u1) {
                onNotifyPointerButton(o, m, i1, u1);
            }),
            sdbus::registerMethod("NotifyPointerAxis").implementedAs([this](sdbus::ObjectPath o, std::unordered_map<std::string, sdbus::Variant> m, double d1, double d2) {
                onNotifyPointerAxis(o, m, d1, d2);
            }),
            sdbus::registerMethod("NotifyPointerAxisDiscrete")
                .implementedAs(
                    [this](sdbus::ObjectPath o, std::unordered_map<std::string, sdbus::Variant> m, uint32_t u1, int32_t i1) { onNotifyPointerAxisDiscrete(o, m, u1, i1); }),
            sdbus::registerMethod("NotifyKeyboardKeycode").implementedAs([this](sdbus::ObjectPath o, std::unordered_map<std::string, sdbus::Variant> m, int32_t i1, uint32_t u1) {
                onNotifyKeyboardKeycode(o, m, i1, u1);
            }),
            sdbus::registerMethod("NotifyKeyboardKeysym").implementedAs([this](sdbus::ObjectPath o, std::unordered_map<std::string, sdbus::Variant> m, int32_t i1, uint32_t u1) {
                onNotifyKeyboardKeysym(o, m, i1, u1);
            }),
            sdbus::registerProperty("AvailableDeviceTypes").withGetter([this]() { return availableDeviceTypes(); }),
            sdbus::registerProperty("version").withGetter([this]() { return version(); }))
        .forInterface(INTERFACE_NAME);

    Debug::log(LOG, "[remotedesktop] registered");
}

CRemoteDesktopPortal::~CRemoteDesktopPortal() {
    if (m_xkbKeymap)
        xkb_keymap_unref(m_xkbKeymap);
    if (m_xkbCtx)
        xkb_context_unref(m_xkbCtx);
}

// ─── Session management ──────────────────────────────────────────

CRemoteDesktopPortal::SSession::~SSession() {
    if (xkbState)
        xkb_state_unref(xkbState);
    if (eisFd >= 0)
        g_pPortalManager->removeFdFromEventLoop(eisFd);
    if (eisPointer) {
        eis_device_remove(eisPointer);
        eis_device_unref(eisPointer);
    }
    if (eisKeyboard) {
        eis_device_remove(eisKeyboard);
        eis_device_unref(eisKeyboard);
    }
    if (eisSeat)
        eis_seat_unref(eisSeat);
    if (eis) {
        eis_unref(eis);
        eis = nullptr;
    }
    eisFd = -1;
}

void CRemoteDesktopPortal::removeEISPointerDevice(SSession* session, bool notifyClient) {
    if (!session->eisPointer)
        return;

    if (notifyClient)
        eis_device_remove(session->eisPointer);
    eis_device_unref(session->eisPointer);
    session->eisPointer       = nullptr;
    session->eisPointerWidth  = 0;
    session->eisPointerHeight = 0;
}

void CRemoteDesktopPortal::createEISPointerDevice(SSession* session) {
    if (!session->eisSeat)
        return;

    auto* dev = eis_seat_new_device(session->eisSeat);
    if (!dev)
        return;

    eis_device_configure_type(dev, EIS_DEVICE_TYPE_VIRTUAL);
    eis_device_configure_name(dev, "Hyprland virtual pointer");
    eis_device_configure_capability(dev, EIS_DEVICE_CAP_POINTER);
    eis_device_configure_capability(dev, EIS_DEVICE_CAP_POINTER_ABSOLUTE);
    eis_device_configure_capability(dev, EIS_DEVICE_CAP_BUTTON);
    eis_device_configure_capability(dev, EIS_DEVICE_CAP_SCROLL);

    uint32_t extentW = 3840, extentH = 2160;
    if (g_pPortalManager)
        g_pPortalManager->getOutputExtents(extentW, extentH);
    if (auto* region = eis_device_new_region(dev)) {
        eis_region_set_offset(region, 0, 0);
        eis_region_set_size(region, extentW, extentH);
        eis_region_add(region);
        eis_region_unref(region);
    }

    eis_device_add(dev);
    eis_device_resume(dev);
    session->eisPointer       = dev;
    session->eisPointerWidth  = extentW;
    session->eisPointerHeight = extentH;
    Debug::log(LOG, "[remotedesktop] EIS pointer device added & resumed with region {}x{}", extentW, extentH);
}

void CRemoteDesktopPortal::removeEISKeyboardDevice(SSession* session, bool notifyClient) {
    if (!session->eisKeyboard)
        return;

    if (notifyClient)
        eis_device_remove(session->eisKeyboard);
    eis_device_unref(session->eisKeyboard);
    session->eisKeyboard = nullptr;
}

void CRemoteDesktopPortal::createEISKeyboardDevice(SSession* session) {
    if (!session->eisSeat)
        return;

    auto* dev = eis_seat_new_device(session->eisSeat);
    if (!dev)
        return;

    eis_device_configure_type(dev, EIS_DEVICE_TYPE_VIRTUAL);
    eis_device_configure_name(dev, "Hyprland virtual keyboard");
    eis_device_configure_capability(dev, EIS_DEVICE_CAP_KEYBOARD);

    // Provide an XKB keymap so the EIS client can process keyboard events.
    // Without this, ei_device_keyboard_get_keymap() returns NULL on the client,
    // causing a crash.
    bool keymapAdded = false;
    {
        auto* ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (ctx) {
            auto* km = xkb_keymap_new_from_names(ctx, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
            if (km) {
                char* kmStr = xkb_keymap_get_as_string(km, XKB_KEYMAP_FORMAT_TEXT_V1);
                if (kmStr) {
                    char tmpName[] = "/tmp/xdph-kb-XXXXXX";
                    int  kfd       = mkstemp(tmpName);
                    if (kfd >= 0) {
                        size_t sz = strlen(kmStr) + 1;
                        if (write(kfd, kmStr, sz) == sc<ssize_t>(sz) && lseek(kfd, 0, SEEK_SET) >= 0) {
                            // Use the libeis API: create keymap, add to device, release our ref
                            auto* eisKm = eis_device_new_keymap(dev, EIS_KEYMAP_TYPE_XKB, kfd, sz);
                            if (eisKm) {
                                eis_keymap_add(eisKm);
                                eis_keymap_unref(eisKm);
                                keymapAdded = true;
                            }
                        }
                        close(kfd);
                        unlink(tmpName);
                    }
                    free(kmStr);
                }
                xkb_keymap_unref(km);
            }
            xkb_context_unref(ctx);
        }
    }

    if (!keymapAdded) {
        Debug::log(ERR, "[remotedesktop] failed to create EIS keyboard keymap, withholding device");
        eis_device_unref(dev);
        return;
    }

    eis_device_add(dev);
    eis_device_resume(dev);
    session->eisKeyboard = dev;
    Debug::log(LOG, "[remotedesktop] EIS keyboard device added & resumed");
}

void CRemoteDesktopPortal::updateEISPointerRegions() {
    uint32_t extentW = 3840, extentH = 2160;
    if (g_pPortalManager)
        g_pPortalManager->getOutputExtents(extentW, extentH);

    for (auto& session : m_vSessions) {
        if (!session->eisPointer || (session->eisPointerWidth == extentW && session->eisPointerHeight == extentH))
            continue;

        removeEISPointerDevice(session.get());
        createEISPointerDevice(session.get());
    }
}

dbUasv CRemoteDesktopPortal::onCreateSession(sdbus::ObjectPath requestHandle, sdbus::ObjectPath sessionHandle, std::string appID,
                                             std::unordered_map<std::string, sdbus::Variant> opts) {
    Debug::log(LOG, "[remotedesktop] New session: appid={} req={} sess={}", appID, requestHandle, sessionHandle);

    const auto PSESSION = m_vSessions.emplace_back(std::make_unique<SSession>(appID, requestHandle, sessionHandle)).get();

    PSESSION->session            = createDBusSession(sessionHandle);
    PSESSION->session->onDestroy = [this, sessionHandle]() { destroySession(sessionHandle); };
    PSESSION->request            = createDBusRequest(requestHandle);
    PSESSION->request->onDestroy = [PSESSION]() { PSESSION->request.release(); };

    if (g_pPortalManager->m_sPortals.screencopy)
        g_pPortalManager->m_sPortals.screencopy->createRemoteDesktopSession(appID, sessionHandle);

    std::unordered_map<std::string, sdbus::Variant> results;
    std::unordered_map<std::string, sdbus::Variant> res;
    // session_handle must be serialized as a string, NOT an ObjectPath,
    // because the GLib-based frontend portal uses g_variant_dict_lookup(,"&s")
    // to extract it from the response variant dict.
    res["session_handle"] = sdbus::Variant{std::string{sessionHandle}};

    Debug::log(LOG, "[remotedesktop] CreateSession returning for appid={}", appID);
    return {0, res};
}

dbUasv CRemoteDesktopPortal::onSelectDevices(sdbus::ObjectPath requestHandle, sdbus::ObjectPath sessionHandle, std::string appID,
                                             std::unordered_map<std::string, sdbus::Variant> opts) {
    const auto PSESSION = getSession(sessionHandle);

    if (!PSESSION) {
        Debug::log(ERR, "[remotedesktop] SelectDevices: no session");
        return {1, {}};
    }

    for (auto& [k, v] : opts) {
        if (k == "types") {
            PSESSION->deviceTypes = v.get<uint32_t>();
            Debug::log(LOG, "[remotedesktop] devices selected: {}", PSESSION->deviceTypes);
        } else if (k == "persist_mode") {
            PSESSION->persistMode = v.get<uint32_t>();
            Debug::log(LOG, "[remotedesktop] persist mode selected: {}", PSESSION->persistMode);
        } else if (k == "restore_data") {
            // xdg-desktop-portal swaps the app-facing `restore_token` for our own
            // `restore_data` blob before it reaches this backend, so this is the only
            // key we ever get to see.
            try {
                const auto DATA = v.get<SRestoreData>();
                if (DATA.get<0>() != RESTORE_DATA_VENDOR || DATA.get<1>() != RESTORE_DATA_VERSION)
                    Debug::log(LOG, "[remotedesktop] ignoring foreign restore data from {} v{}", DATA.get<0>(), DATA.get<1>());
                else {
                    const auto MAP = DATA.get<2>().get<std::unordered_map<std::string, sdbus::Variant>>();
                    if (const auto IT = MAP.find(RESTORE_DATA_TOKEN_KEY); IT != MAP.end())
                        PSESSION->restoreToken = IT->second.get<std::string>();
                }
            } catch (const std::exception& e) { Debug::log(WARN, "[remotedesktop] malformed restore data: {}", e.what()); }
        }
    }

    if (PSESSION->persistMode == 1) {
        Debug::log(WARN, "[remotedesktop] transient persistence is unsupported; downgrading to no persistence");
        PSESSION->persistMode = 0;
    }

    if (PSESSION->deviceTypes == 0)
        PSESSION->deviceTypes = availableDeviceTypes();

    PSESSION->deviceTypes &= availableDeviceTypes();
    if (PSESSION->deviceTypes == 0) {
        Debug::log(ERR, "[remotedesktop] none of the requested device types are available");
        return {2, {}};
    }

    // Deliberately not gated on persistMode: persist_mode describes how the *new*
    // session should be remembered, and says nothing about whether the token the app
    // just presented is good.
    PSESSION->restored = validateRestoreToken(PSESSION->restoreToken, appID, PSESSION->deviceTypes);
    if (PSESSION->restored)
        Debug::log(LOG, "[remotedesktop] valid restore token, skipping consent dialog");

    return {0, {}};
}

dbUasv CRemoteDesktopPortal::onStart(sdbus::ObjectPath requestHandle, sdbus::ObjectPath sessionHandle, std::string appID, std::string parentWindow,
                                     std::unordered_map<std::string, sdbus::Variant> opts) {
    const auto PSESSION = getSession(sessionHandle);

    if (!PSESSION) {
        Debug::log(ERR, "[remotedesktop] Start: no session");
        return {1, {}};
    }

    if (PSESSION->started) {
        Debug::log(WARN, "[remotedesktop] session already started");
        return {0, {}};
    }

    if (PSESSION->deviceTypes == 0)
        PSESSION->deviceTypes = availableDeviceTypes();

    wl_display* display = g_pPortalManager->m_sWaylandConnection.display;
    if (!display) {
        Debug::log(ERR, "[remotedesktop] no Wayland display");
        return {2, {}};
    }

    // Persisting is the user's call, not the app's: `persist_mode` only gets us as far
    // as offering the checkbox in the consent dialog.
    bool persistAllowed = false;
    if (!PSESSION->restored) {
        if (!promptForRemoteDesktopConsent(appID, PSESSION->deviceTypes, PSESSION->persistMode == 2, &persistAllowed)) {
            Debug::log(LOG, "[remotedesktop] user denied remote-control access");
            return {1, {}};
        }
    } else
        persistAllowed = true; // the grant being restored was consented to already

    bool initialized = true;

    // Create virtual pointer
    if (PSESSION->deviceTypes & 2) {
        if (!m_sState.pointer) {
            Debug::log(ERR, "[remotedesktop] no virtual pointer manager");
            initialized = false;
        } else if (!g_pPortalManager->m_sWaylandConnection.seat) {
            Debug::log(ERR, "[remotedesktop] no Wayland seat");
            initialized = false;
        } else {
            wl_proxy* seatProxy = g_pPortalManager->m_sWaylandConnection.seat->proxy();
            wl_proxy* vpProxy   = m_sState.pointer->sendCreateVirtualPointer(seatProxy);
            if (vpProxy) {
                PSESSION->virtualPointer = makeShared<CCZwlrVirtualPointerV1>(vpProxy);
                wl_display_flush(display);
                Debug::log(LOG, "[remotedesktop] virtual pointer created");
            } else
                initialized = false;
        }
    }

    // Create virtual keyboard
    if (PSESSION->deviceTypes & 1) {
        if (!m_sState.keyboard) {
            Debug::log(ERR, "[remotedesktop] no virtual keyboard manager");
            initialized = false;
        } else if (!g_pPortalManager->m_sWaylandConnection.seat) {
            Debug::log(ERR, "[remotedesktop] no Wayland seat");
            initialized = false;
        } else {
            wl_proxy* seatProxy = g_pPortalManager->m_sWaylandConnection.seat->proxy();
            wl_proxy* vkProxy   = m_sState.keyboard->sendCreateVirtualKeyboard(seatProxy);
            if (vkProxy) {
                PSESSION->virtualKeyboard = makeShared<CCZwpVirtualKeyboardV1>(vkProxy);
                bool keymapSent           = false;

                // Send a keymap to the compositor. Required before any key events,
                // otherwise the compositor sends a protocol error:
                //   "Key event received before a keymap was set"
                auto* ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
                if (ctx) {
                    auto* km = xkb_keymap_new_from_names(ctx, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
                    if (km) {
                        char* kmStr = xkb_keymap_get_as_string(km, XKB_KEYMAP_FORMAT_TEXT_V1);
                        if (kmStr) {
                            char tmpName[] = "/tmp/xdph-kb-XXXXXX";
                            int  kfd       = mkstemp(tmpName);
                            if (kfd >= 0) {
                                size_t sz = strlen(kmStr) + 1;
                                if (write(kfd, kmStr, sz) == sc<ssize_t>(sz) && lseek(kfd, 0, SEEK_SET) >= 0) {
                                    PSESSION->virtualKeyboard->sendKeymap(1 /* WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 */, kfd, sz);
                                    keymapSent = true;
                                }
                                close(kfd);
                                unlink(tmpName);
                            }
                            free(kmStr);
                        }
                        xkb_keymap_unref(km);
                    }
                    xkb_context_unref(ctx);
                }

                if (keymapSent) {
                    PSESSION->xkbState = xkb_state_new(m_xkbKeymap);
                    if (!PSESSION->xkbState)
                        initialized = false;
                    else {
                        wl_display_flush(display);
                        Debug::log(LOG, "[remotedesktop] virtual keyboard created with keymap");
                    }
                } else
                    initialized = false;
            } else
                initialized = false;
        }
    }

    if (!initialized) {
        if (PSESSION->xkbState) {
            xkb_state_unref(PSESSION->xkbState);
            PSESSION->xkbState = nullptr;
        }
        PSESSION->virtualPointer.reset();
        PSESSION->virtualKeyboard.reset();
        Debug::log(ERR, "[remotedesktop] failed to initialize all requested devices");
        return {2, {}};
    }

    std::unordered_map<std::string, sdbus::Variant> results;
    const bool                                      persistSession = PSESSION->persistMode == 2 && persistAllowed;
    if (g_pPortalManager->m_sPortals.screencopy && !g_pPortalManager->m_sPortals.screencopy->startRemoteDesktopSession(sessionHandle, persistSession, results)) {
        if (PSESSION->xkbState) {
            xkb_state_unref(PSESSION->xkbState);
            PSESSION->xkbState = nullptr;
        }
        PSESSION->virtualPointer.reset();
        PSESSION->virtualKeyboard.reset();
        Debug::log(ERR, "[remotedesktop] failed to start the selected screencast");
        return {2, {}};
    }

    PSESSION->started = true;
    // Must be a string, not ObjectPath — frontend expects GVariant string type
    results["session_handle"] = sdbus::Variant{std::string{sessionHandle}};
    results["devices"]        = sdbus::Variant{PSESSION->deviceTypes};
    if (!persistSession) {
        results.erase("restore_data");
        results.erase("persist_mode");
    }

    if (!persistSession && PSESSION->restored) {
        // The app restored a grant but no longer wants one kept; honour that.
        if (revokeRestoreToken(PSESSION->restoreToken))
            Debug::log(LOG, "[remotedesktop] revoked restore token, app asked for no persistence");
        else
            Debug::log(WARN, "[remotedesktop] failed to revoke restore token");
    } else if (persistSession) {
        // A restored grant is already in the store — hand the same one back rather than
        // appending a duplicate entry on every restore.
        const auto TOKEN = PSESSION->restored ? PSESSION->restoreToken : issueRestoreToken(appID, PSESSION->deviceTypes);
        if (TOKEN.empty())
            Debug::log(WARN, "[remotedesktop] failed to persist restore token");
        else {
            // The screencast half of a combined session may already have written its own
            // blob into results; merge into it rather than clobbering it.
            std::unordered_map<std::string, sdbus::Variant> restoreData;
            if (const auto IT = results.find("restore_data"); IT != results.end()) {
                try {
                    const auto EXISTING = IT->second.get<SRestoreData>();
                    if (EXISTING.get<0>() == RESTORE_DATA_VENDOR && EXISTING.get<1>() == RESTORE_DATA_VERSION)
                        restoreData = EXISTING.get<2>().get<std::unordered_map<std::string, sdbus::Variant>>();
                } catch (const std::exception& e) { Debug::log(WARN, "[remotedesktop] could not merge screencast restore data: {}", e.what()); }
            }

            restoreData[RESTORE_DATA_TOKEN_KEY] = sdbus::Variant{TOKEN};
            results["restore_data"]             = sdbus::Variant{SRestoreData{RESTORE_DATA_VENDOR, RESTORE_DATA_VERSION, sdbus::Variant{restoreData}}};
            if (!results.contains("persist_mode"))
                results["persist_mode"] = sdbus::Variant{PSESSION->persistMode};

            Debug::log(LOG, "[remotedesktop] {} restore token", PSESSION->restored ? "reused" : "issued");
        }
    }

    return {0, results};
}

// ─── ConnectToEIS (libei path) ───────────────────────────────────

sdbus::UnixFd CRemoteDesktopPortal::onConnectToEIS(sdbus::ObjectPath sessionHandle, std::string appID, std::unordered_map<std::string, sdbus::Variant> opts) {
    const auto PSESSION = getSession(sessionHandle);

    if (!PSESSION || !PSESSION->started) {
        Debug::log(ERR, "[remotedesktop] ConnectToEIS: no session for {}", std::string(sessionHandle));
        return sdbus::UnixFd{-1};
    }

    if (PSESSION->eis) {
        Debug::log(ERR, "[remotedesktop] ConnectToEIS: session already has an EIS connection");
        return sdbus::UnixFd{-1};
    }

    // Create EIS context
    PSESSION->eis = eis_new(this);
    if (!PSESSION->eis) {
        Debug::log(ERR, "[remotedesktop] failed to create EIS context");
        return sdbus::UnixFd{-1};
    }

    eis_set_user_data(PSESSION->eis, this);

    // Set up fd backend (events are dispatched via processEISEvents)
    if (eis_setup_backend_fd(PSESSION->eis) != 0) {
        Debug::log(ERR, "[remotedesktop] eis_setup_backend_fd failed");
        eis_unref(PSESSION->eis);
        PSESSION->eis = nullptr;
        return sdbus::UnixFd{-1};
    }

    // Get client fd to pass to caller
    int clientFd = eis_backend_fd_add_client(PSESSION->eis);
    if (clientFd < 0) {
        Debug::log(ERR, "[remotedesktop] eis_backend_fd_add_client failed");
        eis_unref(PSESSION->eis);
        PSESSION->eis = nullptr;
        return sdbus::UnixFd{-1};
    }

    // Get the EIS fd to poll for events
    PSESSION->eisFd = eis_get_fd(PSESSION->eis);

    Debug::log(LOG, "[remotedesktop] ConnectToEIS CALLED: client_fd={}, eis_fd={}", clientFd, PSESSION->eisFd);

    // Register the EIS fd with PortalManager's poll loop
    g_pPortalManager->addFdToEventLoop(PSESSION->eisFd, POLLIN, [this] { processEISEvents(); });

    return sdbus::UnixFd{clientFd};
}

// ─── Input notification handlers ─────────────────────────────────

void CRemoteDesktopPortal::onNotifyPointerMotion(sdbus::ObjectPath sessionHandle, std::unordered_map<std::string, sdbus::Variant> opts, double dx, double dy) {
    const auto PSESSION = getSession(sessionHandle);
    if (!PSESSION || !PSESSION->virtualPointer)
        return;

    Debug::log(TRACE, "[remotedesktop] NotifyPointerMotion: dx={}, dy={}", dx, dy);
    PSESSION->virtualPointer->sendMotion(currentTimeMs(), wl_fixed_from_double(dx), wl_fixed_from_double(dy));
    PSESSION->virtualPointer->sendFrame();
    wl_display_flush(g_pPortalManager->m_sWaylandConnection.display);
}

void CRemoteDesktopPortal::onNotifyPointerMotionAbsolute(sdbus::ObjectPath sessionHandle, std::unordered_map<std::string, sdbus::Variant> opts, uint32_t stream, double x,
                                                         double y) {
    const auto PSESSION = getSession(sessionHandle);
    if (!PSESSION || !PSESSION->virtualPointer)
        return;

    uint32_t mappedX = 0, mappedY = 0, extentW = 3840, extentH = 2160;
    if (!g_pPortalManager->m_sPortals.screencopy ||
        !g_pPortalManager->m_sPortals.screencopy->mapRemoteDesktopCoordinates(sessionHandle, stream, x, y, mappedX, mappedY, extentW, extentH)) {
        Debug::log(WARN, "[remotedesktop] cannot map absolute motion for stream {}", stream);
        return;
    }

    Debug::log(TRACE, "[remotedesktop] NotifyPointerMotionAbsolute: stream={} x={}, y={} mapped={},{} extents={}x{}", stream, x, y, mappedX, mappedY, extentW, extentH);
    PSESSION->virtualPointer->sendMotionAbsolute(currentTimeMs(), mappedX, mappedY, extentW, extentH);
    PSESSION->virtualPointer->sendFrame();
    wl_display_flush(g_pPortalManager->m_sWaylandConnection.display);
}

void CRemoteDesktopPortal::onNotifyPointerButton(sdbus::ObjectPath sessionHandle, std::unordered_map<std::string, sdbus::Variant> opts, int32_t button, uint32_t state) {
    const auto PSESSION = getSession(sessionHandle);
    if (!PSESSION || !PSESSION->virtualPointer)
        return;

    PSESSION->virtualPointer->sendButton(currentTimeMs(), button, state);
    PSESSION->virtualPointer->sendFrame();
    wl_display_flush(g_pPortalManager->m_sWaylandConnection.display);
}

void CRemoteDesktopPortal::onNotifyPointerAxis(sdbus::ObjectPath sessionHandle, std::unordered_map<std::string, sdbus::Variant> opts, double dx, double dy) {
    const auto PSESSION = getSession(sessionHandle);
    if (!PSESSION || !PSESSION->virtualPointer)
        return;

    uint32_t time = currentTimeMs();
    if (dy != 0.0) {
        PSESSION->virtualPointer->sendAxisSource(2);
        PSESSION->virtualPointer->sendAxis(time, 0, wl_fixed_from_double(-dy));
        PSESSION->axisActiveY = true;
    }
    if (dx != 0.0) {
        PSESSION->virtualPointer->sendAxisSource(2);
        PSESSION->virtualPointer->sendAxis(time, 1, wl_fixed_from_double(dx));
        PSESSION->axisActiveX = true;
    }

    const auto FINISH = opts.find("finish");
    if (FINISH != opts.end() && FINISH->second.get<bool>()) {
        if (PSESSION->axisActiveY)
            PSESSION->virtualPointer->sendAxisStop(time, 0);
        if (PSESSION->axisActiveX)
            PSESSION->virtualPointer->sendAxisStop(time, 1);
        PSESSION->axisActiveX = false;
        PSESSION->axisActiveY = false;
    }
    PSESSION->virtualPointer->sendFrame();
    wl_display_flush(g_pPortalManager->m_sWaylandConnection.display);
}

void CRemoteDesktopPortal::onNotifyPointerAxisDiscrete(sdbus::ObjectPath sessionHandle, std::unordered_map<std::string, sdbus::Variant> opts, uint32_t axis, int32_t steps) {
    const auto PSESSION = getSession(sessionHandle);
    if (!PSESSION || !PSESSION->virtualPointer)
        return;
    if (axis > 1) {
        Debug::log(WARN, "[remotedesktop] ignoring discrete scroll with invalid axis {}", axis);
        return;
    }

    uint32_t time = currentTimeMs();
    if (axis == 0)
        steps = -steps;
    PSESSION->virtualPointer->sendAxisSource(0);
    PSESSION->virtualPointer->sendAxisDiscrete(time, axis, wl_fixed_from_int(steps * 15), steps);
    PSESSION->virtualPointer->sendFrame();
    wl_display_flush(g_pPortalManager->m_sWaylandConnection.display);
}

void CRemoteDesktopPortal::onNotifyKeyboardKeycode(sdbus::ObjectPath sessionHandle, std::unordered_map<std::string, sdbus::Variant> opts, int32_t keycode, uint32_t state) {
    const auto PSESSION = getSession(sessionHandle);
    if (!PSESSION || !PSESSION->virtualKeyboard)
        return;

    PSESSION->virtualKeyboard->sendKey(currentTimeMs(), keycode, state);
    xkb_state_update_key(PSESSION->xkbState, keycode + 8, state == 1 ? XKB_KEY_DOWN : XKB_KEY_UP);
    sendModifiers(PSESSION->virtualKeyboard.get(), PSESSION->xkbState, activeKeysymModifiers(PSESSION->keysymModifiers));
    wl_display_flush(g_pPortalManager->m_sWaylandConnection.display);
}

void CRemoteDesktopPortal::onNotifyKeyboardKeysym(sdbus::ObjectPath sessionHandle, std::unordered_map<std::string, sdbus::Variant> opts, int32_t keysym, uint32_t state) {
    const auto PSESSION = getSession(sessionHandle);
    if (!PSESSION || !PSESSION->virtualKeyboard)
        return;

    const auto PRESSED = PSESSION->keysymKeycodes.find(keysym);
    const auto KEY     = state != 1 && PRESSED != PSESSION->keysymKeycodes.end() ?
        PRESSED->second :
        keycodeFromKeysym(keysym, xkb_state_serialize_layout(PSESSION->xkbState, XKB_STATE_LAYOUT_EFFECTIVE));
    if (!KEY.keycode) {
        Debug::log(WARN, "[remotedesktop] keysym 0x{:x} not found in keymap", keysym);
        return;
    }

    if (state == 1) {
        PSESSION->keysymModifiers[keysym] = KEY.modifiers;
        PSESSION->keysymKeycodes[keysym]  = KEY;
    }

    sendModifiers(PSESSION->virtualKeyboard.get(), PSESSION->xkbState, activeKeysymModifiers(PSESSION->keysymModifiers) | KEY.modifiers, KEY.layout);
    PSESSION->virtualKeyboard->sendKey(currentTimeMs(), KEY.keycode, state);
    xkb_state_update_key(PSESSION->xkbState, KEY.keycode + 8, state == 1 ? XKB_KEY_DOWN : XKB_KEY_UP);
    if (state != 1) {
        PSESSION->keysymModifiers.erase(keysym);
        PSESSION->keysymKeycodes.erase(keysym);
    }
    sendModifiers(PSESSION->virtualKeyboard.get(), PSESSION->xkbState, activeKeysymModifiers(PSESSION->keysymModifiers));
    wl_display_flush(g_pPortalManager->m_sWaylandConnection.display);
}

// ─── EIS event processing ───────────────────────────────────────

void CRemoteDesktopPortal::processEISEvents() {

    for (auto& s : m_vSessions) {
        if (!s->eis)
            continue;

        // Dispatch to process incoming data
        eis_dispatch(s->eis);

        // Process events (pull model)
        bool              disconnected = false;
        struct eis_event* event;
        while (!disconnected && (event = eis_get_event(s->eis))) {
            auto     eventType = eis_event_get_type(event);

            auto*    client = eis_event_get_client(event);
            auto*    seat   = eis_event_get_seat(event);
            uint32_t time   = currentTimeMs();

            switch (eventType) {
                case EIS_EVENT_CLIENT_CONNECT: {
                    Debug::log(LOG, "[remotedesktop] EIS client connect");
                    eis_client_connect(client);

                    // Create a seat with the capabilities authorized for this session.
                    auto* newSeat = eis_client_new_seat(client, "kdeconnect-virtual-input");
                    if (!newSeat) {
                        Debug::log(ERR, "[remotedesktop] failed to create EIS seat");
                        break;
                    }
                    if (s->deviceTypes & 2) {
                        eis_seat_configure_capability(newSeat, EIS_DEVICE_CAP_POINTER);
                        eis_seat_configure_capability(newSeat, EIS_DEVICE_CAP_POINTER_ABSOLUTE);
                        eis_seat_configure_capability(newSeat, EIS_DEVICE_CAP_SCROLL);
                        eis_seat_configure_capability(newSeat, EIS_DEVICE_CAP_BUTTON);
                    }
                    if (s->deviceTypes & 1)
                        eis_seat_configure_capability(newSeat, EIS_DEVICE_CAP_KEYBOARD);
                    eis_seat_add(newSeat);
                    eis_seat_unref(newSeat);
                    Debug::log(LOG, "[remotedesktop] EIS seat added with authorized capabilities");
                    break;
                }
                case EIS_EVENT_CLIENT_DISCONNECT: {
                    Debug::log(LOG, "[remotedesktop] EIS client disconnect");
                    removeEISPointerDevice(s.get(), false);
                    removeEISKeyboardDevice(s.get(), false);
                    if (s->eisSeat) {
                        eis_seat_unref(s->eisSeat);
                        s->eisSeat = nullptr;
                    }
                    // The rest of the context is torn down after the event loop:
                    // eis_get_event must not run on an unref'd context.
                    disconnected = true;
                    break;
                }
                case EIS_EVENT_SEAT_BIND: {
                    Debug::log(LOG, "[remotedesktop] EIS seat bind");
                    if (s->eisSeat != seat) {
                        removeEISPointerDevice(s.get());
                        removeEISKeyboardDevice(s.get());
                        if (s->eisSeat)
                            eis_seat_unref(s->eisSeat);
                        s->eisSeat = eis_seat_ref(seat);
                    }

                    // Create and announce a pointer device if requested
                    if ((s->deviceTypes & 2) &&
                        (eis_event_seat_has_capability(event, EIS_DEVICE_CAP_POINTER) || eis_event_seat_has_capability(event, EIS_DEVICE_CAP_POINTER_ABSOLUTE))) {
                        if (!s->eisPointer)
                            createEISPointerDevice(s.get());
                    } else
                        removeEISPointerDevice(s.get());

                    if ((s->deviceTypes & 1) && eis_event_seat_has_capability(event, EIS_DEVICE_CAP_KEYBOARD)) {
                        if (!s->eisKeyboard)
                            createEISKeyboardDevice(s.get());
                    } else
                        removeEISKeyboardDevice(s.get());
                    break;
                }
                case EIS_EVENT_DEVICE_CLOSED: {
                    if (eis_event_get_device(event) == s->eisPointer)
                        removeEISPointerDevice(s.get(), false);
                    else if (eis_event_get_device(event) == s->eisKeyboard)
                        removeEISKeyboardDevice(s.get(), false);
                    break;
                }
                case EIS_EVENT_POINTER_MOTION: {
                    if (s->virtualPointer) {
                        double dx = eis_event_pointer_get_dx(event);
                        double dy = eis_event_pointer_get_dy(event);

                        s->virtualPointer->sendMotion(time, wl_fixed_from_double(dx), wl_fixed_from_double(dy));
                    }
                    break;
                }
                case EIS_EVENT_POINTER_MOTION_ABSOLUTE: {
                    if (s->virtualPointer) {
                        double   x = eis_event_pointer_get_absolute_x(event);
                        double   y = eis_event_pointer_get_absolute_y(event);

                        uint32_t extentW = 3840, extentH = 2160; // fallback
                        if (g_pPortalManager)
                            g_pPortalManager->getOutputExtents(extentW, extentH);
                        s->virtualPointer->sendMotionAbsolute(time, (uint32_t)x, (uint32_t)y, extentW, extentH);
                    }
                    break;
                }
                case EIS_EVENT_BUTTON_BUTTON: {
                    if (s->virtualPointer) {
                        uint32_t button = eis_event_button_get_button(event);
                        uint32_t state  = eis_event_button_get_is_press(event) ? 1 : 0;
                        s->virtualPointer->sendButton(time, button, state);
                    }
                    break;
                }
                case EIS_EVENT_SCROLL_DELTA: {
                    if (s->virtualPointer) {
                        double dx = eis_event_scroll_get_dx(event);
                        double dy = eis_event_scroll_get_dy(event);
                        if (dy != 0.0) {
                            s->virtualPointer->sendAxisSource(2);
                            s->virtualPointer->sendAxis(time, 0, wl_fixed_from_double(-dy));
                        }
                        if (dx != 0.0) {
                            s->virtualPointer->sendAxisSource(2);
                            s->virtualPointer->sendAxis(time, 1, wl_fixed_from_double(dx));
                        }
                    }
                    break;
                }
                case EIS_EVENT_SCROLL_DISCRETE: {
                    if (s->virtualPointer) {
                        s->discreteScrollX += eis_event_scroll_get_discrete_dx(event);
                        s->discreteScrollY += eis_event_scroll_get_discrete_dy(event);
                        const int32_t STEPSX = s->discreteScrollX / 120;
                        const int32_t STEPSY = s->discreteScrollY / 120;
                        s->discreteScrollX %= 120;
                        s->discreteScrollY %= 120;
                        if (STEPSY != 0) {
                            const int32_t VERTICALSTEPS = -STEPSY;
                            s->virtualPointer->sendAxisSource(0);
                            s->virtualPointer->sendAxisDiscrete(time, 0, wl_fixed_from_int(VERTICALSTEPS * 15), VERTICALSTEPS);
                        }
                        if (STEPSX != 0) {
                            s->virtualPointer->sendAxisSource(0);
                            s->virtualPointer->sendAxisDiscrete(time, 1, wl_fixed_from_int(STEPSX * 15), STEPSX);
                        }
                    }
                    break;
                }
                case EIS_EVENT_SCROLL_STOP:
                case EIS_EVENT_SCROLL_CANCEL: {
                    if (s->virtualPointer) {
                        if (eis_event_scroll_get_stop_y(event))
                            s->virtualPointer->sendAxisStop(time, 0);
                        if (eis_event_scroll_get_stop_x(event))
                            s->virtualPointer->sendAxisStop(time, 1);
                    }
                    break;
                }
                case EIS_EVENT_KEYBOARD_KEY: {
                    if (s->virtualKeyboard) {
                        uint32_t key   = eis_event_keyboard_get_key(event);
                        uint32_t state = eis_event_keyboard_get_key_is_press(event) ? 1 : 0;
                        s->virtualKeyboard->sendKey(time, key, state);
                        xkb_state_update_key(s->xkbState, key + 8, state == 1 ? XKB_KEY_DOWN : XKB_KEY_UP);
                        sendModifiers(s->virtualKeyboard.get(), s->xkbState, activeKeysymModifiers(s->keysymModifiers));
                    }
                    break;
                }
                case EIS_EVENT_FRAME: {

                    // Commit all pending events with a frame
                    if (s->virtualPointer)
                        s->virtualPointer->sendFrame();
                    if (s->virtualPointer || s->virtualKeyboard)
                        wl_display_flush(g_pPortalManager->m_sWaylandConnection.display);
                    break;
                }
                default: break;
            }

            eis_event_unref(event);
        }

        if (disconnected) {
            // Tear down the whole context so the dead fd leaves the poll set
            // and a later ConnectToEIS on this session can succeed.
            if (s->eisFd >= 0) {
                g_pPortalManager->removeFdFromEventLoop(s->eisFd);
                s->eisFd = -1;
            }
            eis_unref(s->eis);
            s->eis = nullptr;
        }
    }
}

// ─── Keysym → keycode conversion ─────────────────────────────────

CRemoteDesktopPortal::SKeycode CRemoteDesktopPortal::keycodeFromKeysym(uint32_t sym, xkb_layout_index_t preferredLayout) {
    if (!m_xkbKeymap)
        return {};

    const auto FINDINLAYOUT = [this, sym](xkb_layout_index_t layout) -> SKeycode {
        const auto MIN = xkb_keymap_min_keycode(m_xkbKeymap);
        const auto MAX = xkb_keymap_max_keycode(m_xkbKeymap);

        for (xkb_keycode_t code = MIN; code <= MAX; code++) {
            if (layout >= xkb_keymap_num_layouts_for_key(m_xkbKeymap, code))
                continue;

            const auto LEVELS = xkb_keymap_num_levels_for_key(m_xkbKeymap, code, layout);
            for (xkb_level_index_t level = 0; level < LEVELS; level++) {
                const xkb_keysym_t* syms;
                const int           NSYMS = xkb_keymap_key_get_syms_by_level(m_xkbKeymap, code, layout, level, &syms);
                for (int i = 0; i < NSYMS; i++) {
                    if (syms[i] != sc<xkb_keysym_t>(sym))
                        continue;

                    xkb_mod_mask_t masks[8]  = {0};
                    const auto     MASKCOUNT = xkb_keymap_key_get_mods_for_level(m_xkbKeymap, code, layout, level, masks, std::size(masks));
                    return {
                        .keycode   = code - 8,
                        .modifiers = MASKCOUNT > 0 ? masks[0] : 0,
                        .layout    = layout,
                    };
                }
            }
        }

        return {};
    };

    const auto LAYOUTCOUNT = xkb_keymap_num_layouts(m_xkbKeymap);
    if (preferredLayout < LAYOUTCOUNT) {
        const auto KEY = FINDINLAYOUT(preferredLayout);
        if (KEY.keycode)
            return KEY;
    }

    for (xkb_layout_index_t layout = 0; layout < LAYOUTCOUNT; layout++) {
        if (layout == preferredLayout)
            continue;

        const auto KEY = FINDINLAYOUT(layout);
        if (KEY.keycode)
            return KEY;
    }

    return {};
}

// ─── Properties ──────────────────────────────────────────────────

uint32_t CRemoteDesktopPortal::availableDeviceTypes() {
    uint32_t types = 0;
    if (m_sState.keyboard)
        types |= 1;
    if (m_sState.pointer)
        types |= 2;
    return types;
}

uint32_t CRemoteDesktopPortal::version() {
    return 2;
}

// ─── Session lookup ──────────────────────────────────────────────

CRemoteDesktopPortal::SSession* CRemoteDesktopPortal::getSession(const sdbus::ObjectPath& path) {
    for (auto& s : m_vSessions) {
        if (s->sessionHandle == path)
            return s.get();
    }
    return nullptr;
}

void CRemoteDesktopPortal::destroySession(const sdbus::ObjectPath& path) {
    if (g_pPortalManager->m_sPortals.screencopy)
        g_pPortalManager->m_sPortals.screencopy->destroyRemoteDesktopSession(path);

    std::erase_if(m_vSessions, [&](const auto& session) { return session->sessionHandle == path; });
    Debug::log(LOG, "[remotedesktop] session {} destroyed", path.c_str());
}

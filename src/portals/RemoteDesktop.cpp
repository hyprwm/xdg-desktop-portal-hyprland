#include "RemoteDesktop.hpp"
#include "../core/PortalManager.hpp"
#include "../shared/KeyboardState.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <format>
#include <libeis.h>
#include <linux/input.h>
#include <sstream>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <tuple>
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
    Hyprutils::OS::CFileDescriptor fd{open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW)};
    if (!fd.isValid())
        return false;

    struct stat info;
    if (fstat(fd.get(), &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != getuid())
        return false;

    std::array<char, 4096> buffer;
    while (true) {
        const auto count = read(fd.get(), buffer.data(), buffer.size());
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0)
            return false;
        if (count == 0)
            break;
        contents.append(buffer.data(), sc<size_t>(count));
    }
    return close(fd.take()) == 0;
}

// Issuance appends to the store and revocation replaces it, so two backends sharing
// XDG_STATE_HOME (separate session buses) would otherwise lose each other's updates, or
// bring a revoked grant back. The lock lives on its own file because the store's inode
// changes on every revoke, and a lock on it would not exclude the next writer.
class CTokenStoreLock {
  public:
    explicit CTokenStoreLock(const std::filesystem::path& store);

    CTokenStoreLock(const CTokenStoreLock&)            = delete;
    CTokenStoreLock& operator=(const CTokenStoreLock&) = delete;

    bool             locked() const;

  private:
    Hyprutils::OS::CFileDescriptor m_fd; // closing the last descriptor drops the flock
};

CTokenStoreLock::CTokenStoreLock(const std::filesystem::path& store) {
    const auto lockPath = store.string() + ".lock";
    m_fd                = Hyprutils::OS::CFileDescriptor{open(lockPath.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600)};
    if (!m_fd.isValid())
        return;

    struct stat info;
    if (fstat(m_fd.get(), &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != getuid()) {
        m_fd.reset();
        return;
    }

    while (flock(m_fd.get(), LOCK_EX) != 0) {
        if (errno == EINTR)
            continue;
        m_fd.reset();
        return;
    }
}

bool CTokenStoreLock::locked() const {
    return m_fd.isValid();
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

    // Held across read, rewrite and rename, so no other writer can slip in between.
    const CTokenStoreLock LOCK{path};
    if (!LOCK.locked())
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

    const auto                     temporary = path.string() + ".tmp-" + suffix;
    Hyprutils::OS::CFileDescriptor fd{open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600)};
    if (!fd.isValid())
        return false;

    const bool flushed = writeAll(fd.get(), remaining) && fsync(fd.get()) == 0;
    const bool closed  = close(fd.take()) == 0;
    if (!flushed || !closed) {
        unlink(temporary.c_str());
        return false;
    }
    if (rename(temporary.c_str(), path.c_str()) != 0) {
        unlink(temporary.c_str());
        return false;
    }

    const Hyprutils::OS::CFileDescriptor directory{open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
    return directory.isValid() && fsync(directory.get()) == 0;
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

    // Without it, an append can land on the inode a concurrent revoke is replacing.
    const CTokenStoreLock LOCK{path};
    if (!LOCK.locked())
        return {};

    Hyprutils::OS::CFileDescriptor fd{open(path.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600)};
    if (!fd.isValid())
        return {};

    struct stat info;
    if (fstat(fd.get(), &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != getuid() || fchmod(fd.get(), 0600) != 0)
        return {};

    const bool written = writeAll(fd.get(), std::format("{}\t{}\t{}\n", token, appID, deviceTypes));

    // A half-written record has no trailing newline, so the next append would glue
    // itself onto it and take that token down too. Roll back to what we found.
    if (!written || fsync(fd.get()) != 0) {
        if (ftruncate(fd.get(), info.st_size) != 0)
            Debug::log(WARN, "[remotedesktop] could not roll back a partial token record");
        return {};
    }

    if (close(fd.take()) != 0)
        return {};

    // fsync on the file does not make a freshly created file's directory entry
    // durable, and the revoke path already pays for this.
    const Hyprutils::OS::CFileDescriptor directory{open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
    if (directory.isValid() && fsync(directory.get()) != 0)
        Debug::log(WARN, "[remotedesktop] could not flush the token store directory");

    return token;
}

static void sendModifiers(CCZwpVirtualKeyboardV1* keyboard, xkb_state* state, xkb_mod_mask_t extraDepressed, xkb_layout_index_t layout, xkb_mod_mask_t dropped) {
    if (!keyboard || !state)
        return;

    const auto LAYOUT = layout == XKB_LAYOUT_INVALID ? xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_EFFECTIVE) : layout;
    keyboard->sendModifiers((xkb_state_serialize_mods(state, XKB_STATE_MODS_DEPRESSED) & ~dropped) | extraDepressed,
                            xkb_state_serialize_mods(state, XKB_STATE_MODS_LATCHED) & ~dropped, xkb_state_serialize_mods(state, XKB_STATE_MODS_LOCKED) & ~dropped, LAYOUT);
}

// ─── CRemoteDesktopPortal implementation ─────────────────────────

CRemoteDesktopPortal::CRemoteDesktopPortal(SP<CCZwlrVirtualPointerManagerV1> pointerMgr, SP<CCZwpVirtualKeyboardManagerV1> keyboardMgr) {
    m_sState.pointer  = pointerMgr;
    m_sState.keyboard = keyboardMgr;

    // Sessions compile their own keymap out of this context on Start.
    m_xkbCtx.reset(xkb_context_new(XKB_CONTEXT_NO_FLAGS));

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
    ;
}

// ─── Session management ──────────────────────────────────────────

CRemoteDesktopPortal::SSession::SSession(const std::string& app, const sdbus::ObjectPath& req, const sdbus::ObjectPath& sess) :
    appid(app), requestHandle(req), sessionHandle(sess) {
    ;
}

CRemoteDesktopPortal::SSession::~SSession() {
    releaseKeyboardKeys(this);
    releasePointerButtons(this);
    if (eisFd >= 0)
        g_pPortalManager->removeFdFromEventLoop(eisFd);
    if (eisPointer)
        eis_device_remove(eisPointer.get());
    if (eisKeyboard)
        eis_device_remove(eisKeyboard.get());
    // Members drop their references in reverse order: devices, seat, then the context.
    eisFd = -1;
}

void CRemoteDesktopPortal::SEISDeleter::operator()(struct eis* p) const {
    eis_unref(p);
}

void CRemoteDesktopPortal::SEISDeleter::operator()(struct eis_seat* p) const {
    eis_seat_unref(p);
}

void CRemoteDesktopPortal::SEISDeleter::operator()(struct eis_device* p) const {
    eis_device_unref(p);
}

// Regions are advertised per output rather than as one bounding box, so a client can
// tell where the screens actually are. Coordinates stay in the layout's logical space,
// shifted so its top-left is the origin: EIS offsets are unsigned, and the absolute
// motion we forward is normalised against the very same layout box.
std::vector<CRemoteDesktopPortal::SEISRegion> CRemoteDesktopPortal::pointerRegions() {
    std::vector<SEISRegion> regions;

    int32_t                 originX = 0, originY = 0;
    uint32_t                layoutW = 3840, layoutH = 2160;
    g_pPortalManager->getOutputLayout(originX, originY, layoutW, layoutH);

    // Mirror getOutputLayout's preference: logical geometry for every output, or
    // wl_output geometry for every output, never a mix of the two spaces.
    const bool LOGICAL_COMPLETE = g_pPortalManager->hasCompleteLogicalLayout();
    for (const bool LOGICAL : {true, false}) {
        if (LOGICAL && !LOGICAL_COMPLETE)
            continue;

        for (const auto& OUTPUT : g_pPortalManager->getAllOutputs()) {
            int32_t x = 0, y = 0, w = 0, h = 0;
            if (!(LOGICAL ? OUTPUT->logicalGeometry(x, y, w, h) : OUTPUT->fallbackGeometry(x, y, w, h)))
                continue;

            if (w <= 0 || h <= 0) // a region without a size is invalid to EIS
                continue;

            // wl_output.scale is only the integer buffer hint; with logical geometry the
            // effective (possibly fractional) scale is the native mode over the logical size.
            double scale = OUTPUT->scale > 0.0 ? OUTPUT->scale : 1.0;
            if (LOGICAL && OUTPUT->width > 0) {
                const bool ROTATED = OUTPUT->transform == WL_OUTPUT_TRANSFORM_90 || OUTPUT->transform == WL_OUTPUT_TRANSFORM_270 ||
                    OUTPUT->transform == WL_OUTPUT_TRANSFORM_FLIPPED_90 || OUTPUT->transform == WL_OUTPUT_TRANSFORM_FLIPPED_270;
                scale = sc<double>(ROTATED ? OUTPUT->height : OUTPUT->width) / w;
            }

            regions.emplace_back(SEISRegion{
                .x         = x - originX,
                .y         = y - originY,
                .w         = sc<uint32_t>(w),
                .h         = sc<uint32_t>(h),
                .scale     = scale,
                .mappingId = OUTPUT->name,
            });
        }

        if (!regions.empty())
            return regions;
    }

    // No usable output geometry yet. A pointer with no region at all would lose absolute
    // motion entirely, so fall back to the layout box we normalise against anyway.
    regions.emplace_back(SEISRegion{.w = layoutW, .h = layoutH});
    return regions;
}

void CRemoteDesktopPortal::removeEISPointerDevice(SSession* session, bool notifyClient) {
    if (!session->eisPointer)
        return;

    releasePointerButtons(session);
    resetPointerScroll(session);
    if (notifyClient)
        eis_device_remove(session->eisPointer.get());
    session->eisPointer.reset();
    session->eisPointerRegions.clear();
}

void CRemoteDesktopPortal::createEISPointerDevice(SSession* session) {
    if (!session->eisSeat)
        return;

    auto* dev = eis_seat_new_device(session->eisSeat.get());
    if (!dev)
        return;

    eis_device_configure_type(dev, EIS_DEVICE_TYPE_VIRTUAL);
    eis_device_configure_name(dev, "Hyprland virtual pointer");
    eis_device_configure_capability(dev, EIS_DEVICE_CAP_POINTER);
    eis_device_configure_capability(dev, EIS_DEVICE_CAP_POINTER_ABSOLUTE);
    eis_device_configure_capability(dev, EIS_DEVICE_CAP_BUTTON);
    eis_device_configure_capability(dev, EIS_DEVICE_CAP_SCROLL);

    auto regions = pointerRegions();
    for (const auto& REGION : regions) {
        auto* region = eis_device_new_region(dev);
        if (!region)
            continue;

        eis_region_set_offset(region, sc<uint32_t>(REGION.x), sc<uint32_t>(REGION.y));
        eis_region_set_size(region, REGION.w, REGION.h);
        eis_region_set_physical_scale(region, REGION.scale);
        if (!REGION.mappingId.empty())
            eis_region_set_mapping_id(region, REGION.mappingId.c_str());
        eis_region_add(region);
        eis_region_unref(region);
    }

    eis_device_add(dev);
    eis_device_resume(dev);
    session->eisPointer.reset(dev);
    session->eisPointerRegions = std::move(regions);
    Debug::log(LOG, "[remotedesktop] EIS pointer device added & resumed with {} region(s)", session->eisPointerRegions.size());
}

void CRemoteDesktopPortal::removeEISKeyboardDevice(SSession* session, bool notifyClient) {
    if (!session->eisKeyboard)
        return;

    releaseKeyboardKeys(session);
    if (notifyClient)
        eis_device_remove(session->eisKeyboard.get());
    session->eisKeyboard.reset();
}

void CRemoteDesktopPortal::createEISKeyboardDevice(SSession* session) {
    if (!session->eisSeat)
        return;

    // The client needs a keymap to make sense of the keycodes it sends; without one,
    // ei_device_keyboard_get_keymap() hands it NULL and it typically crashes.
    if (!session->keymap.fd.isValid()) {
        Debug::log(ERR, "[remotedesktop] session has no keymap, withholding the EIS keyboard device");
        return;
    }

    auto* dev = eis_seat_new_device(session->eisSeat.get());
    if (!dev)
        return;

    eis_device_configure_type(dev, EIS_DEVICE_TYPE_VIRTUAL);
    eis_device_configure_name(dev, "Hyprland virtual keyboard");
    eis_device_configure_capability(dev, EIS_DEVICE_CAP_KEYBOARD);

    // libeis takes its own copy of the fd, so the session keeps ownership of it.
    auto* eisKm = eis_device_new_keymap(dev, EIS_KEYMAP_TYPE_XKB, session->keymap.fd.get(), session->keymap.size);
    if (!eisKm) {
        Debug::log(ERR, "[remotedesktop] failed to create EIS keyboard keymap, withholding device");
        eis_device_unref(dev);
        return;
    }

    eis_keymap_add(eisKm);
    eis_keymap_unref(eisKm);

    eis_device_add(dev);
    eis_device_resume(dev);
    session->eisKeyboard.reset(dev);
    Debug::log(LOG, "[remotedesktop] EIS keyboard device added & resumed");
}

void CRemoteDesktopPortal::updateEISPointerRegions() {
    const auto REGIONS = pointerRegions();

    for (auto& session : m_vSessions) {
        if (!session->eisPointer || session->eisPointerRegions == REGIONS)
            continue;

        // Regions are immutable once added, so a layout change means a new device.
        removeEISPointerDevice(session.get());
        createEISPointerDevice(session.get());
    }
}

bool CRemoteDesktopPortal::acquireKeymap(SKeymap& keymap) {
    keymap = {};

    if (!m_xkbCtx)
        return false;

    const auto& SOURCE = g_pPortalManager->getCompositorKeymap();
    if (SOURCE.fd.isValid() && SOURCE.size > 0) {
        // wl_keyboard only allows a private mapping of this fd, and we want our own
        // descriptor regardless: the mirror is replaced whenever the user's layout
        // changes, and a session has to keep emulating the map it was started with.
        void* data = mmap(nullptr, SOURCE.size, PROT_READ, MAP_PRIVATE, SOURCE.fd.get(), 0);
        if (data == MAP_FAILED)
            Debug::log(WARN, "[remotedesktop] could not map the compositor keymap: {}", strerror(errno));
        else {
            const auto                                   UNMAP = [size = SOURCE.size](void* p) { munmap(p, size); };
            const std::unique_ptr<void, decltype(UNMAP)> MAPPING{data, UNMAP};
            std::string                                  text{sc<const char*>(MAPPING.get()), SOURCE.size};

            // The keymap arrives NUL-terminated; xkbcommon wants the text without it.
            if (const auto NUL = text.find('\0'); NUL != std::string::npos)
                text.resize(NUL);

            std::unique_ptr<xkb_keymap, decltype(&xkb_keymap_unref)> compiled{
                xkb_keymap_new_from_string(m_xkbCtx.get(), text.c_str(), XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS), xkb_keymap_unref};
            Hyprutils::OS::CFileDescriptor fd{fcntl(SOURCE.fd.get(), F_DUPFD_CLOEXEC, 0)};
            if (compiled && fd.isValid()) {
                keymap.fd     = std::move(fd);
                keymap.size   = SOURCE.size;
                keymap.keymap = std::move(compiled);
                Debug::log(LOG, "[remotedesktop] session emulating with the compositor keymap ({} bytes)", SOURCE.size);
                return true;
            }

            Debug::log(WARN, "[remotedesktop] could not adopt the compositor keymap, falling back to a generated one");
        }
    }

    std::unique_ptr<xkb_keymap, decltype(&xkb_keymap_unref)> generated{xkb_keymap_new_from_names(m_xkbCtx.get(), nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS), xkb_keymap_unref};
    if (!generated) {
        Debug::log(ERR, "[remotedesktop] could not generate a default keymap");
        return false;
    }

    const std::unique_ptr<char, decltype(&free)> SERIALIZED{xkb_keymap_get_as_string(generated.get(), XKB_KEYMAP_FORMAT_TEXT_V1), free};
    if (!SERIALIZED) {
        Debug::log(ERR, "[remotedesktop] could not serialize the generated keymap");
        return false;
    }

    // Keep the trailing NUL: both the compositor and libeis expect the size to cover it.
    const std::string TEXT{SERIALIZED.get(), strlen(SERIALIZED.get()) + 1};

    // Everything that reads this fd mmaps it, so the file can go straight after
    // creation rather than sitting in /tmp for the life of the session.
    char                           tmpName[] = "/tmp/xdph-kb-XXXXXX";
    Hyprutils::OS::CFileDescriptor fd{mkstemp(tmpName)};
    if (!fd.isValid()) {
        Debug::log(ERR, "[remotedesktop] could not create a keymap file: {}", strerror(errno));
        return false;
    }
    unlink(tmpName);

    if (!writeAll(fd.get(), TEXT)) {
        Debug::log(ERR, "[remotedesktop] could not write the generated keymap");
        return false;
    }

    keymap.fd     = std::move(fd);
    keymap.size   = sc<uint32_t>(TEXT.size());
    keymap.keymap = std::move(generated);
    Debug::log(LOG, "[remotedesktop] no compositor keymap available, session emulating with a generated default");
    return true;
}

dbUasv CRemoteDesktopPortal::onCreateSession(sdbus::ObjectPath requestHandle, sdbus::ObjectPath sessionHandle, std::string appID,
                                             std::unordered_map<std::string, sdbus::Variant> opts) {
    Debug::log(LOG, "[remotedesktop] New session: appid={} req={} sess={}", appID, std::string(requestHandle), std::string(sessionHandle));

    const auto PSESSION = m_vSessions.emplace_back(std::make_unique<SSession>(appID, requestHandle, sessionHandle)).get();

    PSESSION->session            = createDBusSession(sessionHandle);
    PSESSION->session->onDestroy = [this, sessionHandle]() { destroySession(sessionHandle); };
    PSESSION->request            = createDBusRequest(requestHandle);
    // A closed request stays exported, so a later Close can arrive after the session is
    // gone: look the session up instead of capturing a pointer that may dangle.
    PSESSION->request->onDestroy = [this, sessionHandle]() {
        if (const auto SESSION = getSession(sessionHandle))
            SESSION->request.release();
    };

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
                    PSESSION->restoreData = MAP;
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
    if (PSESSION->restored) {
        Debug::log(LOG, "[remotedesktop] valid restore token, skipping consent dialog");
        // The screencast half was saved into the same blob; only a still-valid grant may
        // restore it. NOTE: this needs SelectDevices before SelectSources, as clients do.
        if (g_pPortalManager->m_sPortals.screencopy)
            g_pPortalManager->m_sPortals.screencopy->restoreRemoteDesktopSelection(sessionHandle, PSESSION->restoreData);
    }

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

                // The compositor needs a keymap before any key event, otherwise it kills
                // us with "Key event received before a keymap was set". Handing it the
                // map it is already using keeps emulated keycodes meaning what the user's
                // own layout says they mean.
                if (!acquireKeymap(PSESSION->keymap))
                    initialized = false;
                else {
                    PSESSION->virtualKeyboard->sendKeymap(WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, PSESSION->keymap.fd.get(), PSESSION->keymap.size);
                    PSESSION->xkbState = std::make_unique<CKeyboardState>(PSESSION->keymap.keymap.get());
                    if (!PSESSION->xkbState->get())
                        initialized = false;
                    else {
                        wl_display_flush(display);
                        Debug::log(LOG, "[remotedesktop] virtual keyboard created with keymap");
                    }
                }
            } else
                initialized = false;
        }
    }

    if (!initialized) {
        PSESSION->xkbState.reset();
        PSESSION->keymap = {};
        PSESSION->virtualPointer.reset();
        PSESSION->virtualKeyboard.reset();
        Debug::log(ERR, "[remotedesktop] failed to initialize all requested devices");
        return {2, {}};
    }

    std::unordered_map<std::string, sdbus::Variant> results;
    const bool                                      persistSession = PSESSION->persistMode == 2 && persistAllowed;
    if (g_pPortalManager->m_sPortals.screencopy && !g_pPortalManager->m_sPortals.screencopy->startRemoteDesktopSession(sessionHandle, persistSession, results)) {
        PSESSION->xkbState.reset();
        PSESSION->keymap = {};
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
        if (TOKEN.empty()) {
            // Without the grant the screencast half would never be restored either, so
            // don't hand out a restore token at all.
            Debug::log(WARN, "[remotedesktop] failed to persist restore token");
            results.erase("restore_data");
            results.erase("persist_mode");
        } else {
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
        throw sdbus::Error{sdbus::Error::Name{"org.freedesktop.portal.Error.Failed"}, "No started remote desktop session found"};
    }

    if (PSESSION->eis) {
        Debug::log(ERR, "[remotedesktop] ConnectToEIS: session already has an EIS connection");
        throw sdbus::Error{sdbus::Error::Name{"org.freedesktop.portal.Error.Failed"}, "Session already has an EIS connection"};
    }

    // Create EIS context
    PSESSION->eis.reset(eis_new(this));
    if (!PSESSION->eis) {
        Debug::log(ERR, "[remotedesktop] failed to create EIS context");
        throw sdbus::Error{sdbus::Error::Name{"org.freedesktop.portal.Error.Failed"}, "Failed to create EIS context"};
    }

    eis_set_user_data(PSESSION->eis.get(), this);

    // Set up fd backend (events are dispatched via processEISEvents)
    if (eis_setup_backend_fd(PSESSION->eis.get()) != 0) {
        Debug::log(ERR, "[remotedesktop] eis_setup_backend_fd failed");
        PSESSION->eis.reset();
        throw sdbus::Error{sdbus::Error::Name{"org.freedesktop.portal.Error.Failed"}, "Failed to set up EIS backend"};
    }

    // Get client fd to pass to caller
    int clientFd = eis_backend_fd_add_client(PSESSION->eis.get());
    if (clientFd < 0) {
        Debug::log(ERR, "[remotedesktop] eis_backend_fd_add_client failed");
        PSESSION->eis.reset();
        throw sdbus::Error{sdbus::Error::Name{"org.freedesktop.portal.Error.Failed"}, "Failed to add EIS client"};
    }

    // Get the EIS fd to poll for events
    PSESSION->eisFd = eis_get_fd(PSESSION->eis.get());

    Debug::log(LOG, "[remotedesktop] ConnectToEIS CALLED: client_fd={}, eis_fd={}", clientFd, PSESSION->eisFd);

    // Register the EIS fd with PortalManager's poll loop
    g_pPortalManager->addFdToEventLoop(PSESSION->eisFd, POLLIN, [this] { processEISEvents(); });

    // Adopt rather than dup: we own clientFd, and a leftover copy here would keep the
    // client end open, so EIS would never see the client disconnect.
    return sdbus::UnixFd{clientFd, sdbus::adopt_fd};
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

    if (state)
        PSESSION->heldButtons.insert(sc<uint32_t>(button));
    else
        PSESSION->heldButtons.erase(sc<uint32_t>(button));
    PSESSION->virtualPointer->sendButton(currentTimeMs(), button, state);
    PSESSION->virtualPointer->sendFrame();
    wl_display_flush(g_pPortalManager->m_sWaylandConnection.display);
}

void CRemoteDesktopPortal::onNotifyPointerAxis(sdbus::ObjectPath sessionHandle, std::unordered_map<std::string, sdbus::Variant> opts, double dx, double dy) {
    const auto PSESSION = getSession(sessionHandle);
    if (!PSESSION || !PSESSION->virtualPointer)
        return;

    // Portal and Wayland scroll deltas use the same coordinate directions.
    uint32_t time = currentTimeMs();
    if (dy != 0.0) {
        PSESSION->virtualPointer->sendAxisSource(2);
        PSESSION->virtualPointer->sendAxis(time, 0, wl_fixed_from_double(dy));
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
    PSESSION->virtualPointer->sendAxisSource(0);
    PSESSION->virtualPointer->sendAxisDiscrete(time, axis, wl_fixed_from_int(steps * 15), steps);
    PSESSION->virtualPointer->sendFrame();
    wl_display_flush(g_pPortalManager->m_sWaylandConnection.display);
}

void CRemoteDesktopPortal::onNotifyKeyboardKeycode(sdbus::ObjectPath sessionHandle, std::unordered_map<std::string, sdbus::Variant> opts, int32_t keycode, uint32_t state) {
    const auto PSESSION = getSession(sessionHandle);
    if (!PSESSION || !PSESSION->virtualKeyboard)
        return;

    sendRawKey(PSESSION, keycode, state, currentTimeMs());
    wl_display_flush(g_pPortalManager->m_sWaylandConnection.display);
}

void CRemoteDesktopPortal::onNotifyKeyboardKeysym(sdbus::ObjectPath sessionHandle, std::unordered_map<std::string, sdbus::Variant> opts, int32_t keysym, uint32_t state) {
    const auto PSESSION = getSession(sessionHandle);
    if (!PSESSION || !PSESSION->virtualKeyboard)
        return;

    const auto PRESSED = PSESSION->keysymKeycodes.find(keysym);
    auto       KEY     = PRESSED != PSESSION->keysymKeycodes.end() ?
        PRESSED->second :
        keycodeFromKeysym(PSESSION->keymap.keymap.get(), keysym, xkb_state_serialize_layout(PSESSION->xkbState->get(), XKB_STATE_LAYOUT_EFFECTIVE));
    if (!KEY.keycode) {
        Debug::log(WARN, "[remotedesktop] keysym 0x{:x} not found in keymap", keysym);
        return;
    }

    // Only the current owner of a key may release it; see takeOverKey().
    if (state != 1 && PRESSED == PSESSION->keysymKeycodes.end())
        return;

    if (state == 1)
        takeOverKey(PSESSION, KEY.keycode, keysym);

    if (state == 1) {
        KEY.keysym                       = keysym;
        KEY.order                        = ++PSESSION->keyPresses;
        KEY.repeats                      = xkb_keymap_key_repeats(PSESSION->keymap.keymap.get(), KEY.keycode + 8);
        PSESSION->keysymKeycodes[keysym] = KEY;
    }

    const auto DROPPED = sendSessionModifiers(PSESSION, KEY);
    PSESSION->virtualKeyboard->sendKey(currentTimeMs(), KEY.keycode, state);
    // Run the key's XKB action at the level just emitted, not the real one.
    PSESSION->xkbState->key(KEY.keycode, state == 1, KEY.modifiers, DROPPED, KEY.layout);
    if (state != 1)
        PSESSION->keysymKeycodes.erase(keysym);
    sendSessionModifiers(PSESSION, {});
    wl_display_flush(g_pPortalManager->m_sWaylandConnection.display);
}

// ─── EIS event processing ───────────────────────────────────────

void CRemoteDesktopPortal::processEISEvents() {

    for (auto& s : m_vSessions) {
        if (!s->eis)
            continue;

        // Dispatch to process incoming data
        eis_dispatch(s->eis.get());

        // Process events (pull model)
        bool              disconnected = false;
        struct eis_event* event;
        while (!disconnected && (event = eis_get_event(s->eis.get()))) {
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
                    s->eisSeat.reset();
                    // The rest of the context is torn down after the event loop:
                    // eis_get_event must not run on an unref'd context.
                    disconnected = true;
                    break;
                }
                case EIS_EVENT_SEAT_BIND: {
                    Debug::log(LOG, "[remotedesktop] EIS seat bind");
                    if (s->eisSeat.get() != seat) {
                        removeEISPointerDevice(s.get());
                        removeEISKeyboardDevice(s.get());
                        s->eisSeat.reset(eis_seat_ref(seat));
                    }

                    // Create and announce a pointer device if requested. libeis fixes a device's
                    // capabilities to the binding at creation, so recreate it when the binding changes.
                    uint32_t pointerCaps = 0;
                    for (const auto CAP : {EIS_DEVICE_CAP_POINTER, EIS_DEVICE_CAP_POINTER_ABSOLUTE, EIS_DEVICE_CAP_BUTTON, EIS_DEVICE_CAP_SCROLL}) {
                        if (eis_event_seat_has_capability(event, CAP))
                            pointerCaps |= CAP;
                    }

                    if ((s->deviceTypes & 2) && pointerCaps) {
                        if (s->eisPointer && s->eisPointerCaps != pointerCaps)
                            removeEISPointerDevice(s.get());
                        if (!s->eisPointer)
                            createEISPointerDevice(s.get());
                        s->eisPointerCaps = pointerCaps;
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
                    if (eis_event_get_device(event) == s->eisPointer.get())
                        removeEISPointerDevice(s.get(), false);
                    else if (eis_event_get_device(event) == s->eisKeyboard.get())
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
                        // Regions are advertised relative to the layout's top-left, so the
                        // coordinates arrive in that same space and only need the extents.
                        uint32_t extentW = 3840, extentH = 2160; // fallback
                        if (g_pPortalManager)
                            g_pPortalManager->getOutputExtents(extentW, extentH);

                        // A client should stay inside a region, but the gaps between them
                        // are reachable coordinates; clamp rather than wrap on the cast.
                        const double X = std::clamp(eis_event_pointer_get_absolute_x(event), 0.0, sc<double>(extentW));
                        const double Y = std::clamp(eis_event_pointer_get_absolute_y(event), 0.0, sc<double>(extentH));

                        s->virtualPointer->sendMotionAbsolute(time, sc<uint32_t>(X), sc<uint32_t>(Y), extentW, extentH);
                    }
                    break;
                }
                case EIS_EVENT_BUTTON_BUTTON: {
                    if (s->virtualPointer) {
                        uint32_t button = eis_event_button_get_button(event);
                        uint32_t state  = eis_event_button_get_is_press(event) ? 1 : 0;
                        if (state)
                            s->heldButtons.insert(button);
                        else
                            s->heldButtons.erase(button);
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
                            s->virtualPointer->sendAxis(time, 0, wl_fixed_from_double(dy));
                            s->axisActiveY = true;
                        }
                        if (dx != 0.0) {
                            s->virtualPointer->sendAxisSource(2);
                            s->virtualPointer->sendAxis(time, 1, wl_fixed_from_double(dx));
                            s->axisActiveX = true;
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
                            s->virtualPointer->sendAxisSource(0);
                            s->virtualPointer->sendAxisDiscrete(time, 0, wl_fixed_from_int(STEPSY * 15), STEPSY);
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
                        if (eis_event_scroll_get_stop_y(event)) {
                            s->virtualPointer->sendAxisStop(time, 0);
                            s->axisActiveY = false;
                        }
                        if (eis_event_scroll_get_stop_x(event)) {
                            s->virtualPointer->sendAxisStop(time, 1);
                            s->axisActiveX = false;
                        }
                    }
                    break;
                }
                case EIS_EVENT_DEVICE_STOP_EMULATING: {
                    // Each device only releases its own held input.
                    if (eis_event_get_device(event) == s->eisKeyboard.get())
                        releaseKeyboardKeys(s.get());
                    else if (eis_event_get_device(event) == s->eisPointer.get()) {
                        releasePointerButtons(s.get());
                        resetPointerScroll(s.get());
                    }
                    break;
                }
                case EIS_EVENT_KEYBOARD_KEY: {
                    if (s->virtualKeyboard) {
                        uint32_t key   = eis_event_keyboard_get_key(event);
                        uint32_t state = eis_event_keyboard_get_key_is_press(event) ? 1 : 0;
                        sendRawKey(s.get(), key, state, time);
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
            s->eis.reset();
        }
    }
}

void CRemoteDesktopPortal::releaseKeyboardKeys(SSession* session) {
    if (!session->virtualKeyboard || !session->xkbState)
        return;

    const auto TIME = currentTimeMs();
    for (const auto KEY : session->xkbState->releaseAll())
        session->virtualKeyboard->sendKey(TIME, KEY, 0);
    session->keysymKeycodes.clear();
    session->rawKeys.clear();
    sendSessionModifiers(session, {});
    wl_display_flush(g_pPortalManager->m_sWaylandConnection.display);
}

void CRemoteDesktopPortal::releasePointerButtons(SSession* session) {
    if (session->heldButtons.empty())
        return;

    if (session->virtualPointer) {
        const auto TIME = currentTimeMs();
        for (const auto BUTTON : session->heldButtons)
            session->virtualPointer->sendButton(TIME, BUTTON, 0);
        session->virtualPointer->sendFrame();
        wl_display_flush(g_pPortalManager->m_sWaylandConnection.display);
    }
    session->heldButtons.clear();
}

void CRemoteDesktopPortal::resetPointerScroll(SSession* session) {
    session->discreteScrollX = 0;
    session->discreteScrollY = 0;
    if (!session->axisActiveX && !session->axisActiveY)
        return;

    if (session->virtualPointer) {
        const auto TIME = currentTimeMs();
        if (session->axisActiveY)
            session->virtualPointer->sendAxisStop(TIME, 0);
        if (session->axisActiveX)
            session->virtualPointer->sendAxisStop(TIME, 1);
        session->virtualPointer->sendFrame();
        wl_display_flush(g_pPortalManager->m_sWaylandConnection.display);
    }
    session->axisActiveX = false;
    session->axisActiveY = false;
}

CRemoteDesktopPortal::SKeycode CRemoteDesktopPortal::newestHeldKeysym(const SSession* session) {
    SKeycode newest;
    for (const auto& [_, key] : session->keysymKeycodes) {
        if (std::tie(key.repeats, key.order) > std::tie(newest.repeats, newest.order))
            newest = key;
    }

    // A newer repeating raw key owns the repeat, and it was asked for in the real state.
    for (const auto& [code, order] : session->rawKeys) {
        if (xkb_keymap_key_repeats(session->keymap.keymap.get(), code + 8) && (!newest.repeats || order > newest.order))
            return {};
    }
    return newest;
}

// The compositor sees one key per keycode, whichever way it was pressed: keysyms such as
// 'a' and 'A', or a keysym and a raw keycode, can land on the same one. The newest press
// takes the key over, and the previous owner's release is then dropped.
void CRemoteDesktopPortal::takeOverKey(SSession* session, uint32_t keycode, int32_t keysym) {
    bool       held  = session->rawKeys.erase(keycode) != 0;
    const auto ALIAS = std::ranges::find_if(session->keysymKeycodes, [&](const auto& other) { return other.first != keysym && other.second.keycode == keycode; });
    if (ALIAS != session->keysymKeycodes.end()) {
        session->keysymKeycodes.erase(ALIAS);
        held = true;
    }

    if (!held)
        return;

    session->virtualKeyboard->sendKey(currentTimeMs(), keycode, 0);
    session->xkbState->key(keycode, false);
}

void CRemoteDesktopPortal::sendRawKey(SSession* session, uint32_t keycode, uint32_t state, uint32_t time) {
    if (state != 1) {
        if (!session->rawKeys.erase(keycode))
            return;
    } else {
        if (session->rawKeys.contains(keycode))
            return;

        takeOverKey(session, keycode, XKB_KEY_NoSymbol);
        session->rawKeys[keycode] = ++session->keyPresses;
        // Drop a held keysym's synthetic level before the key lands, or it would pick it up.
        if (xkb_keymap_key_repeats(session->keymap.keymap.get(), keycode + 8))
            sendSessionModifiers(session, {});
    }

    session->xkbState->key(keycode, state == 1);
    session->virtualKeyboard->sendKey(time, keycode, state);
    sendSessionModifiers(session, {});
}

// Only one level can be in effect: OR-ing every held keysym's modifiers would turn a
// 'b' pressed while a Shift-selected 'A' is still down into 'B'. Older held keysyms
// don't repeat anymore, so letting the newest win is safe. Non-repeating keys such as
// Control_L don't take over the repeat, so the newest repeating key wins over them.
// Conflicts are resolved against the current state rather than at press time, so a
// real modifier or lock that changes while the keysym is held can't flip its repeats.
xkb_mod_mask_t CRemoteDesktopPortal::sendSessionModifiers(SSession* session, const SKeycode& pending) {
    const auto KEY     = pending.keycode ? pending : newestHeldKeysym(session);
    const auto DROPPED = KEY.keycode && KEY.keysym != XKB_KEY_NoSymbol ? session->xkbState->conflictingMods(KEY.keycode, KEY.keysym, KEY.modifiers, KEY.layout) : 0;
    sendModifiers(session->virtualKeyboard.get(), session->xkbState->get(), KEY.modifiers, KEY.layout, DROPPED);
    return DROPPED;
}

// ─── Keysym → keycode conversion ─────────────────────────────────

CRemoteDesktopPortal::SKeycode CRemoteDesktopPortal::keycodeFromKeysym(struct xkb_keymap* keymap, uint32_t sym, xkb_layout_index_t preferredLayout) {
    if (!keymap)
        return {};

    const auto FINDINLAYOUT = [keymap, sym](xkb_layout_index_t layout) -> SKeycode {
        const auto MIN = xkb_keymap_min_keycode(keymap);
        const auto MAX = xkb_keymap_max_keycode(keymap);

        for (xkb_keycode_t code = MIN; code <= MAX; code++) {
            if (layout >= xkb_keymap_num_layouts_for_key(keymap, code))
                continue;

            const auto LEVELS = xkb_keymap_num_levels_for_key(keymap, code, layout);
            for (xkb_level_index_t level = 0; level < LEVELS; level++) {
                const xkb_keysym_t* syms;
                const int           NSYMS = xkb_keymap_key_get_syms_by_level(keymap, code, layout, level, &syms);
                for (int i = 0; i < NSYMS; i++) {
                    if (syms[i] != sc<xkb_keysym_t>(sym))
                        continue;

                    xkb_mod_mask_t masks[8]  = {0};
                    const auto     MASKCOUNT = xkb_keymap_key_get_mods_for_level(keymap, code, layout, level, masks, std::size(masks));
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

    const auto LAYOUTCOUNT = xkb_keymap_num_layouts(keymap);
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
    Debug::log(LOG, "[remotedesktop] session {} destroyed, {} still alive", path.c_str(), m_vSessions.size());
}

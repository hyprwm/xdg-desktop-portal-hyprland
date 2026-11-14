#pragma once

#include <memory>
#include <unordered_set>
#include <vector>
#include <sdbus-c++/sdbus-c++.h>
#include <xkbcommon/xkbcommon.h>

#include "../includes.hpp"
#include "../dbusDefines.hpp"
#include "../shared/Session.hpp"
#include "../helpers/Log.hpp"

#include "wlr-virtual-pointer-unstable-v1.hpp"
#include "virtual-keyboard-unstable-v1.hpp"

struct eis;
struct eis_client;
struct eis_seat;
struct eis_device;
class CKeyboardState;

class CRemoteDesktopPortal {
  public:
    CRemoteDesktopPortal(SP<CCZwlrVirtualPointerManagerV1> pointerMgr, SP<CCZwpVirtualKeyboardManagerV1> keyboardMgr);
    ~CRemoteDesktopPortal();

    // D-Bus session management
    dbUasv onCreateSession(sdbus::ObjectPath requestHandle, sdbus::ObjectPath sessionHandle, std::string appID, std::unordered_map<std::string, sdbus::Variant> opts);
    dbUasv onSelectDevices(sdbus::ObjectPath requestHandle, sdbus::ObjectPath sessionHandle, std::string appID, std::unordered_map<std::string, sdbus::Variant> opts);
    dbUasv onStart(sdbus::ObjectPath requestHandle, sdbus::ObjectPath sessionHandle, std::string appID, std::string parentWindow,
                   std::unordered_map<std::string, sdbus::Variant> opts);

    // ConnectToEIS (libei-based input path - used by KDE Connect, etc.)
    sdbus::UnixFd onConnectToEIS(sdbus::ObjectPath sessionHandle, std::string appID, std::unordered_map<std::string, sdbus::Variant> opts);

    // Input notification handlers (fire-and-forget, void returns)
    void onNotifyPointerMotion(sdbus::ObjectPath sessionHandle, std::unordered_map<std::string, sdbus::Variant> opts, double dx, double dy);
    void onNotifyPointerMotionAbsolute(sdbus::ObjectPath sessionHandle, std::unordered_map<std::string, sdbus::Variant> opts, uint32_t stream, double x, double y);
    void onNotifyPointerButton(sdbus::ObjectPath sessionHandle, std::unordered_map<std::string, sdbus::Variant> opts, int32_t button, uint32_t state);
    void onNotifyPointerAxis(sdbus::ObjectPath sessionHandle, std::unordered_map<std::string, sdbus::Variant> opts, double dx, double dy);
    void onNotifyPointerAxisDiscrete(sdbus::ObjectPath sessionHandle, std::unordered_map<std::string, sdbus::Variant> opts, uint32_t axis, int32_t steps);
    void onNotifyKeyboardKeycode(sdbus::ObjectPath sessionHandle, std::unordered_map<std::string, sdbus::Variant> opts, int32_t keycode, uint32_t state);
    void onNotifyKeyboardKeysym(sdbus::ObjectPath sessionHandle, std::unordered_map<std::string, sdbus::Variant> opts, int32_t keysym, uint32_t state);

    // D-Bus properties
    uint32_t availableDeviceTypes();
    uint32_t version();

    // EIS event processing (called from the main event loop)
    void processEISEvents();
    void updateEISPointerRegions();

  private:
    struct SKeycode {
        uint32_t           keycode   = 0;
        xkb_mod_mask_t     modifiers = 0;
        xkb_layout_index_t layout    = XKB_LAYOUT_INVALID;
        // The keysym this key was pressed for; its conflicting modifiers are recomputed
        // against the current state whenever modifiers are sent.
        xkb_keysym_t keysym = XKB_KEY_NoSymbol;
        // Press sequence number; the newest held keysym is the one a client repeats.
        uint64_t order = 0;
        // Whether the key repeats; a held modifier never takes over a client's key repeat.
        bool repeats = false;
    };

    // One EIS region per output, in the layout's logical coordinate space with the
    // layout's top-left at the origin (EIS offsets are unsigned, so a monitor left of
    // or above 0,0 could not be expressed otherwise). Advertising the outputs
    // individually rather than one bounding box keeps a client's absolute pointer out
    // of the dead space an L-shaped or staggered layout leaves between screens.
    struct SEISRegion {
        int32_t  x     = 0;
        int32_t  y     = 0;
        uint32_t w     = 0;
        uint32_t h     = 0;
        double   scale = 1.0;
        // Matches the ScreenCast stream's mapping_id (the output name), so a client can
        // pair a captured monitor with its absolute-pointer region.
        std::string mappingId;

        bool        operator==(const SEISRegion& other) const = default;
    };

    // The keymap a session emulates with: the compositor's own whenever we can read it,
    // a generated default otherwise. Sessions hold their own copy so a keymap change
    // mid-session can't desync keycode lookups from what the compositor was handed.
    struct SKeymap {
        int                fd     = -1;
        uint32_t           size   = 0;
        struct xkb_keymap* keymap = nullptr;
    };

    struct SSession {
        SSession(const std::string& app, const sdbus::ObjectPath& req, const sdbus::ObjectPath& sess);
        ~SSession();

        std::string                   appid;
        sdbus::ObjectPath             requestHandle;
        sdbus::ObjectPath             sessionHandle;

        std::unique_ptr<SDBusSession> session;
        std::unique_ptr<SDBusRequest> request;

        CWeakPointer<SSession>        self;

        uint32_t                      deviceTypes = 0; // bitmask: 1=keyboard, 2=pointer, 4=touchscreen
        uint32_t                      persistMode = 0;
        std::string                   restoreToken;
        bool                          restored = false;
        bool                          started  = false;

        // Full restore blob, so a restored grant can hand the screencast half its selection.
        std::unordered_map<std::string, sdbus::Variant> restoreData;

        // Wayland objects (created on Start)
        SP<CCZwlrVirtualPointerV1>            virtualPointer;
        SP<CCZwpVirtualKeyboardV1>            virtualKeyboard;

        std::unique_ptr<CKeyboardState>       xkbState;
        bool                                  axisActiveX     = false;
        bool                                  axisActiveY     = false;
        int32_t                               discreteScrollX = 0;
        int32_t                               discreteScrollY = 0;
        std::unordered_map<int32_t, SKeycode> keysymKeycodes;
        uint64_t                              keysymPresses = 0;
        // Buttons an EIS client is holding, so they can be released if it stops
        // emulating mid-drag instead of leaving the desktop dragging.
        std::unordered_set<uint32_t> eisHeldButtons;

        SKeymap                      keymap;

        // EIS/libei state (created by ConnectToEIS)
        struct eis*             eis            = nullptr;
        struct eis_seat*        eisSeat        = nullptr;
        struct eis_device*      eisPointer     = nullptr;
        struct eis_device*      eisKeyboard    = nullptr;
        uint32_t                eisPointerCaps = 0; // pointer-family caps bound when eisPointer was created
        std::vector<SEISRegion> eisPointerRegions;
        int                     eisFd    = -1; // fd to poll for EIS events
        bool                    eisReady = false;
    };

    SSession*               getSession(const sdbus::ObjectPath& path);
    void                    destroySession(const sdbus::ObjectPath& path);
    void                    createEISPointerDevice(SSession* session);
    void                    removeEISPointerDevice(SSession* session, bool notifyClient = true);
    void                    createEISKeyboardDevice(SSession* session);
    void                    removeEISKeyboardDevice(SSession* session, bool notifyClient = true);
    std::vector<SEISRegion> pointerRegions();

    // Take a private copy of the keymap emulated input should speak.
    bool        acquireKeymap(SKeymap& keymap);
    static void releaseKeymap(SKeymap& keymap);
    static void releaseKeyboardKeys(SSession* session);
    static void releasePointerButtons(SSession* session);
    // End any scroll an EIS pointer left running and drop its partial wheel steps.
    static void resetPointerScroll(SSession* session);
    // The most recently pressed keysym still held, or an empty key if none is.
    static SKeycode newestHeldKeysym(const SSession* session);
    // Send the session's modifiers with `pending`'s level applied on top, or else the
    // newest held keysym's, so key repeat keeps resolving the symbol it asked for.
    static void sendSessionModifiers(SSession* session, const SKeycode& pending);

    // Keysym → keycode conversion (via xkbcommon)
    static SKeycode                        keycodeFromKeysym(struct xkb_keymap* keymap, uint32_t sym, xkb_layout_index_t preferredLayout);

    std::unique_ptr<sdbus::IObject>        m_pObject;
    std::vector<std::unique_ptr<SSession>> m_vSessions;

    struct {
        SP<CCZwlrVirtualPointerManagerV1> pointer;
        SP<CCZwpVirtualKeyboardManagerV1> keyboard;
    } m_sState;

    // XKB context used to compile the keymaps handed to sessions
    struct xkb_context*        m_xkbCtx = nullptr;

    const sdbus::InterfaceName INTERFACE_NAME = sdbus::InterfaceName{"org.freedesktop.impl.portal.RemoteDesktop"};
    const sdbus::ObjectPath    OBJECT_PATH    = sdbus::ObjectPath{"/org/freedesktop/portal/desktop"};
};

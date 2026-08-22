#pragma once

#include <memory>
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
    };

    struct SSession {
        SSession(const std::string& app, const sdbus::ObjectPath& req, const sdbus::ObjectPath& sess) : appid(app), requestHandle(req), sessionHandle(sess) {}
        ~SSession();

        std::string                   appid;
        sdbus::ObjectPath             requestHandle;
        sdbus::ObjectPath             sessionHandle;

        std::unique_ptr<SDBusSession> session;
        std::unique_ptr<SDBusRequest> request;

        CWeakPointer<SSession>        self;

        uint32_t                      deviceTypes = 0; // bitmask: 1=keyboard, 2=pointer, 4=touchscreen
        bool                          started     = false;

        // Wayland objects (created on Start)
        SP<CCZwlrVirtualPointerV1>                  virtualPointer;
        SP<CCZwpVirtualKeyboardV1>                  virtualKeyboard;

        struct xkb_state*                           xkbState        = nullptr;
        bool                                        axisActiveX     = false;
        bool                                        axisActiveY     = false;
        int32_t                                     discreteScrollX = 0;
        int32_t                                     discreteScrollY = 0;
        std::unordered_map<int32_t, xkb_mod_mask_t> keysymModifiers;
        std::unordered_map<int32_t, SKeycode>       keysymKeycodes;

        // EIS/libei state (created by ConnectToEIS)
        struct eis*        eis              = nullptr;
        struct eis_seat*   eisSeat          = nullptr;
        struct eis_device* eisPointer       = nullptr;
        struct eis_device* eisKeyboard      = nullptr;
        uint32_t           eisPointerWidth  = 0;
        uint32_t           eisPointerHeight = 0;
        int                eisFd            = -1; // fd to poll for EIS events
        bool               eisReady         = false;
    };

    SSession* getSession(const sdbus::ObjectPath& path);
    void      destroySession(const sdbus::ObjectPath& path);
    void      createEISPointerDevice(SSession* session);
    void      removeEISPointerDevice(SSession* session, bool notifyClient = true);
    void      createEISKeyboardDevice(SSession* session);
    void      removeEISKeyboardDevice(SSession* session, bool notifyClient = true);

    // Keysym → keycode conversion (via xkbcommon)
    SKeycode                               keycodeFromKeysym(uint32_t sym, xkb_layout_index_t preferredLayout);

    std::unique_ptr<sdbus::IObject>        m_pObject;
    std::vector<std::unique_ptr<SSession>> m_vSessions;

    struct {
        SP<CCZwlrVirtualPointerManagerV1> pointer;
        SP<CCZwpVirtualKeyboardManagerV1> keyboard;
    } m_sState;

    // XKB state for keysym → keycode conversion
    struct xkb_context*        m_xkbCtx    = nullptr;
    struct xkb_keymap*         m_xkbKeymap = nullptr;

    const sdbus::InterfaceName INTERFACE_NAME = sdbus::InterfaceName{"org.freedesktop.impl.portal.RemoteDesktop"};
    const sdbus::ObjectPath    OBJECT_PATH    = sdbus::ObjectPath{"/org/freedesktop/portal/desktop"};
};

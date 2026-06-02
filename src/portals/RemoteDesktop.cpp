#include "RemoteDesktop.hpp"
#include "../core/PortalManager.hpp"

#include <chrono>
#include <unistd.h>
#include <sys/socket.h>
#include <libeis.h>
#include <linux/input.h>
#include <xkbcommon/xkbcommon.h>

// Helper: get current time in ms for Wayland events
static uint32_t currentTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Map linux evdev keycodes to xkb modifier bits.
// Standard xkb modifier indices: Shift=0, Lock=1, Control=2, Mod1(Alt)=3, Mod4(Super)=6
static uint32_t xkbModForEvdev(int evdevKeycode) {
    switch (evdevKeycode) {
    case 42:  case 54:  return 1 << 0; // KEY_LEFTSHIFT, KEY_RIGHTSHIFT
    case 29:  case 97:  return 1 << 2; // KEY_LEFTCTRL, KEY_RIGHTCTRL
    case 56:  case 100: return 1 << 3; // KEY_LEFTALT,  KEY_RIGHTALT
    case 125: case 126: return 1 << 6; // KEY_LEFTMETA, KEY_RIGHTMETA
    default: return 0;
    }
}

static void updateModifiers(CCZwpVirtualKeyboardV1* vk, uint32_t modDepressed) {
    if (vk)
        vk->sendModifiers(modDepressed, 0, 0, 0);
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
        ->addVTable(sdbus::registerMethod("CreateSession")
                        .implementedAs([this](sdbus::ObjectPath o1, sdbus::ObjectPath o2, std::string s,
                                              std::unordered_map<std::string, sdbus::Variant> m) { return onCreateSession(o1, o2, s, m); }),
                    sdbus::registerMethod("SelectDevices")
                        .implementedAs([this](sdbus::ObjectPath o1, sdbus::ObjectPath o2, std::string s,
                                              std::unordered_map<std::string, sdbus::Variant> m) { return onSelectDevices(o1, o2, s, m); }),
                    sdbus::registerMethod("Start")
                        .implementedAs([this](sdbus::ObjectPath o1, sdbus::ObjectPath o2, std::string s1, std::string s2,
                                              std::unordered_map<std::string, sdbus::Variant> m) { return onStart(o1, o2, s1, s2, m); }),
                    sdbus::registerMethod("ConnectToEIS")
                        .implementedAs([this](sdbus::ObjectPath o, std::string s, std::unordered_map<std::string, sdbus::Variant> m) {
                            return onConnectToEIS(o, s, m);
                        }),
                    sdbus::registerMethod("NotifyPointerMotion")
                        .implementedAs([this](sdbus::ObjectPath o, std::unordered_map<std::string, sdbus::Variant> m, double d1, double d2) {
                            onNotifyPointerMotion(o, m, d1, d2);
                        }),
                    sdbus::registerMethod("NotifyPointerButton")
                        .implementedAs([this](sdbus::ObjectPath o, std::unordered_map<std::string, sdbus::Variant> m, int32_t i1, uint32_t u1) {
                            onNotifyPointerButton(o, m, i1, u1);
                        }),
                    sdbus::registerMethod("NotifyPointerAxis")
                        .implementedAs([this](sdbus::ObjectPath o, std::unordered_map<std::string, sdbus::Variant> m, double d1, double d2) {
                            onNotifyPointerAxis(o, m, d1, d2);
                        }),
                    sdbus::registerMethod("NotifyPointerAxisDiscrete")
                        .implementedAs([this](sdbus::ObjectPath o, std::unordered_map<std::string, sdbus::Variant> m, uint32_t u1, int32_t i1) {
                            onNotifyPointerAxisDiscrete(o, m, u1, i1);
                        }),
                    sdbus::registerMethod("NotifyKeyboardKeycode")
                        .implementedAs([this](sdbus::ObjectPath o, std::unordered_map<std::string, sdbus::Variant> m, int32_t i1, uint32_t u1) {
                            onNotifyKeyboardKeycode(o, m, i1, u1);
                        }),
                    sdbus::registerMethod("NotifyKeyboardKeysym")
                        .implementedAs([this](sdbus::ObjectPath o, std::unordered_map<std::string, sdbus::Variant> m, int32_t i1, uint32_t u1) {
                            onNotifyKeyboardKeysym(o, m, i1, u1);
                        }),
                    sdbus::registerProperty("AvailableDeviceTypes").withGetter([this]() { return availableDeviceTypes(); }),
                    sdbus::registerProperty("version").withGetter([this]() { return version(); }))
        .forInterface(INTERFACE_NAME);

    Debug::log(LOG, "[remotedesktop] registered");
}

// ─── Session management ──────────────────────────────────────────

CRemoteDesktopPortal::SSession::~SSession() {
    if (eisFd >= 0)
        g_pPortalManager->removeExtraPollFd(eisFd);
    if (eis) {
        eis_unref(eis);
        eis = nullptr;
    }
    if (eisFd >= 0) {
        close(eisFd);
        eisFd = -1;
    }
}

dbUasv CRemoteDesktopPortal::onCreateSession(sdbus::ObjectPath requestHandle, sdbus::ObjectPath sessionHandle, std::string appID,
                                             std::unordered_map<std::string, sdbus::Variant> opts) {
    Debug::log(LOG, "[remotedesktop] New session: appid={} req={} sess={}", appID, requestHandle, sessionHandle);

    const auto PSESSION = m_vSessions.emplace_back(std::make_unique<SSession>(appID, requestHandle, sessionHandle)).get();

    PSESSION->session            = createDBusSession(sessionHandle);
    PSESSION->session->onDestroy = [PSESSION]() { PSESSION->session.release(); };
    PSESSION->request            = createDBusRequest(requestHandle);
    PSESSION->request->onDestroy = [PSESSION]() { PSESSION->request.release(); };

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
        }
    }

    if (PSESSION->deviceTypes == 0) {
        Debug::log(ERR, "[remotedesktop] no device types selected, defaulting to pointer+keyboard");
        PSESSION->deviceTypes = 3;
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

    wl_display* display = g_pPortalManager->m_sWaylandConnection.display;
    if (!display) {
        Debug::log(ERR, "[remotedesktop] no Wayland display");
        return {2, {}};
    }

    // Create virtual pointer
    if (PSESSION->deviceTypes & 2) {
        if (!m_sState.pointer) {
            Debug::log(ERR, "[remotedesktop] no virtual pointer manager");
        } else if (!g_pPortalManager->m_sWaylandConnection.seat) {
            Debug::log(ERR, "[remotedesktop] no Wayland seat");
        } else {
            wl_proxy* seatProxy = g_pPortalManager->m_sWaylandConnection.seat->proxy();
            wl_proxy* vpProxy   = m_sState.pointer->sendCreateVirtualPointer(seatProxy);
            if (vpProxy) {
                PSESSION->virtualPointer = makeShared<CCZwlrVirtualPointerV1>(vpProxy);
                wl_display_flush(display);
                Debug::log(LOG, "[remotedesktop] virtual pointer created");
            }
        }
    }

    // Create virtual keyboard
    if (PSESSION->deviceTypes & 1) {
        if (!m_sState.keyboard) {
            Debug::log(ERR, "[remotedesktop] no virtual keyboard manager");
        } else if (!g_pPortalManager->m_sWaylandConnection.seat) {
            Debug::log(ERR, "[remotedesktop] no Wayland seat");
        } else {
            wl_proxy* seatProxy = g_pPortalManager->m_sWaylandConnection.seat->proxy();
            wl_proxy* vkProxy   = m_sState.keyboard->sendCreateVirtualKeyboard(seatProxy);
            if (vkProxy) {
                PSESSION->virtualKeyboard = makeShared<CCZwpVirtualKeyboardV1>(vkProxy);

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
                            int kfd = mkstemp(tmpName);
                            if (kfd >= 0) {
                                size_t sz = strlen(kmStr);
                                write(kfd, kmStr, sz);
                                lseek(kfd, 0, SEEK_SET);
                                PSESSION->virtualKeyboard->sendKeymap(1 /* WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 */, kfd, sz);
                                close(kfd);
                                unlink(tmpName);
                            }
                            free(kmStr);
                        }
                        xkb_keymap_unref(km);
                    }
                    xkb_context_unref(ctx);
                }

                wl_display_flush(display);
                Debug::log(LOG, "[remotedesktop] virtual keyboard created with keymap");
            }
        }
    }

    PSESSION->started = true;

    std::unordered_map<std::string, sdbus::Variant> results;
    // Must be a string, not ObjectPath — frontend expects GVariant string type
    results["session_handle"] = sdbus::Variant{std::string{sessionHandle}};
    results["devices"]        = sdbus::Variant{PSESSION->deviceTypes};

    return {0, results};
}

// ─── ConnectToEIS (libei path) ───────────────────────────────────

sdbus::UnixFd CRemoteDesktopPortal::onConnectToEIS(sdbus::ObjectPath sessionHandle, std::string appID,
                                                   std::unordered_map<std::string, sdbus::Variant> opts) {
    const auto PSESSION = getSession(sessionHandle);

    if (!PSESSION) {
        Debug::log(ERR, "[remotedesktop] ConnectToEIS: no session for {}", std::string(sessionHandle));
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
    g_pPortalManager->addExtraPollFd(PSESSION->eisFd);

    return sdbus::UnixFd{clientFd};
}

// ─── Input notification handlers ─────────────────────────────────

void CRemoteDesktopPortal::onNotifyPointerMotion(sdbus::ObjectPath sessionHandle, std::unordered_map<std::string, sdbus::Variant> opts, double dx,
                                                 double dy) {
    const auto PSESSION = getSession(sessionHandle);
    if (!PSESSION || !PSESSION->virtualPointer)
        return;

    Debug::log(TRACE, "[remotedesktop] NotifyPointerMotion: dx={}, dy={}", dx, dy);
    PSESSION->virtualPointer->sendMotion(currentTimeMs(), wl_fixed_from_double(dx), wl_fixed_from_double(dy));
    PSESSION->virtualPointer->sendFrame();
    wl_display_flush(g_pPortalManager->m_sWaylandConnection.display);
}

void CRemoteDesktopPortal::onNotifyPointerButton(sdbus::ObjectPath sessionHandle, std::unordered_map<std::string, sdbus::Variant> opts, int32_t button,
                                                 uint32_t state) {
    const auto PSESSION = getSession(sessionHandle);
    if (!PSESSION || !PSESSION->virtualPointer)
        return;

    PSESSION->virtualPointer->sendButton(currentTimeMs(), button, state);
    PSESSION->virtualPointer->sendFrame();
    wl_display_flush(g_pPortalManager->m_sWaylandConnection.display);
}

void CRemoteDesktopPortal::onNotifyPointerAxis(sdbus::ObjectPath sessionHandle, std::unordered_map<std::string, sdbus::Variant> opts, double dx,
                                               double dy) {
    const auto PSESSION = getSession(sessionHandle);
    if (!PSESSION || !PSESSION->virtualPointer)
        return;

    uint32_t time = currentTimeMs();
    if (dy != 0.0) {
        PSESSION->virtualPointer->sendAxisSource(2);
        PSESSION->virtualPointer->sendAxis(time, 0, wl_fixed_from_double(-dy));
    }
    if (dx != 0.0) {
        PSESSION->virtualPointer->sendAxisSource(2);
        PSESSION->virtualPointer->sendAxis(time, 1, wl_fixed_from_double(dx));
    }
    PSESSION->virtualPointer->sendFrame();
    wl_display_flush(g_pPortalManager->m_sWaylandConnection.display);
}

void CRemoteDesktopPortal::onNotifyPointerAxisDiscrete(sdbus::ObjectPath sessionHandle, std::unordered_map<std::string, sdbus::Variant> opts,
                                                       uint32_t axis, int32_t steps) {
    const auto PSESSION = getSession(sessionHandle);
    if (!PSESSION || !PSESSION->virtualPointer)
        return;

    uint32_t time = currentTimeMs();
    PSESSION->virtualPointer->sendAxisSource(2);
    PSESSION->virtualPointer->sendAxisDiscrete(time, axis, wl_fixed_from_int(steps * 15), steps);
    PSESSION->virtualPointer->sendFrame();
    wl_display_flush(g_pPortalManager->m_sWaylandConnection.display);
}

void CRemoteDesktopPortal::onNotifyKeyboardKeycode(sdbus::ObjectPath sessionHandle, std::unordered_map<std::string, sdbus::Variant> opts,
                                                   int32_t keycode, uint32_t state) {
    const auto PSESSION = getSession(sessionHandle);
    if (!PSESSION || !PSESSION->virtualKeyboard)
        return;

    

    uint32_t modBit = xkbModForEvdev(keycode);
    if (modBit) {
        if (state == 1)
            PSESSION->modDepressed |= modBit;
        else
            PSESSION->modDepressed &= ~modBit;
    }

    PSESSION->virtualKeyboard->sendKey(currentTimeMs(), keycode, state);
    updateModifiers(PSESSION->virtualKeyboard.get(), PSESSION->modDepressed);
    wl_display_flush(g_pPortalManager->m_sWaylandConnection.display);
}

void CRemoteDesktopPortal::onNotifyKeyboardKeysym(sdbus::ObjectPath sessionHandle, std::unordered_map<std::string, sdbus::Variant> opts,
                                                   int32_t keysym, uint32_t state) {
    const auto PSESSION = getSession(sessionHandle);
    if (!PSESSION || !PSESSION->virtualKeyboard)
        return;

    // Find the base (unshifted) keycode for this keysym.
    // Shift state is managed entirely by the KDE Connect handlePacket code
    // (which sends separate keyboardKeycode(KEY_LEFTSHIFT, ...) calls).
    // The backend must NOT also send shift, or the double-shift causes
    // the modifier to be released prematurely.
    uint32_t keycode = keycodeFromKeysym(keysym, false /* searchLevel0Only */);
    if (!keycode) {
        Debug::log(WARN, "[remotedesktop] keysym 0x{:x} not found in keymap", keysym);
        return;
    }

    
    PSESSION->virtualKeyboard->sendKey(currentTimeMs(), keycode, state);
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
        struct eis_event* event;
        int evCount = 0;
        while ((event = eis_get_event(s->eis))) {
            auto eventType = eis_event_get_type(event);
            
            evCount++;
            auto*    client    = eis_event_get_client(event);
            auto*    seat      = eis_event_get_seat(event);
            uint32_t time      = currentTimeMs();

            switch (eventType) {
            case EIS_EVENT_CLIENT_CONNECT: {
                Debug::log(LOG, "[remotedesktop] EIS client connect");
                eis_client_connect(client);

                // Create a seat with all capabilities we support
                auto* newSeat = eis_client_new_seat(client, "kdeconnect-virtual-input");
                if (!newSeat) {
                    Debug::log(ERR, "[remotedesktop] failed to create EIS seat");
                    break;
                }
                eis_seat_configure_capability(newSeat, EIS_DEVICE_CAP_POINTER);
                eis_seat_configure_capability(newSeat, EIS_DEVICE_CAP_POINTER_ABSOLUTE);
                eis_seat_configure_capability(newSeat, EIS_DEVICE_CAP_KEYBOARD);
                eis_seat_configure_capability(newSeat, EIS_DEVICE_CAP_SCROLL);
                eis_seat_configure_capability(newSeat, EIS_DEVICE_CAP_BUTTON);
                eis_seat_add(newSeat);
                Debug::log(LOG, "[remotedesktop] EIS seat added with all capabilities");
                break;
            }
            case EIS_EVENT_CLIENT_DISCONNECT: {
                Debug::log(LOG, "[remotedesktop] EIS client disconnect");
                break;
            }
            case EIS_EVENT_SEAT_BIND: {
                Debug::log(LOG, "[remotedesktop] EIS seat bind");
                // Create and announce a pointer device if requested
                if (eis_event_seat_has_capability(event, EIS_DEVICE_CAP_POINTER) ||
                    eis_event_seat_has_capability(event, EIS_DEVICE_CAP_POINTER_ABSOLUTE)) {
                    auto* dev = eis_seat_new_device(seat);
                    if (dev) {
                        eis_device_configure_type(dev, EIS_DEVICE_TYPE_VIRTUAL);
                        eis_device_configure_name(dev, "Hyprland virtual pointer");
                        eis_device_configure_capability(dev, EIS_DEVICE_CAP_POINTER);
                        eis_device_configure_capability(dev, EIS_DEVICE_CAP_POINTER_ABSOLUTE);
                        eis_device_configure_capability(dev, EIS_DEVICE_CAP_BUTTON);
                        eis_device_configure_capability(dev, EIS_DEVICE_CAP_SCROLL);
                        eis_device_add(dev);
                        eis_device_start_emulating(dev, 0);
                        Debug::log(LOG, "[remotedesktop] EIS pointer device added & emulating");
                    }
                }
                if (eis_event_seat_has_capability(event, EIS_DEVICE_CAP_KEYBOARD)) {
                    auto* dev = eis_seat_new_device(seat);
                    if (dev) {
                        eis_device_configure_type(dev, EIS_DEVICE_TYPE_VIRTUAL);
                        eis_device_configure_name(dev, "Hyprland virtual keyboard");
                        eis_device_configure_capability(dev, EIS_DEVICE_CAP_KEYBOARD);
                        // Provide an XKB keymap so the EIS client can process keyboard events.
                        // Without this, ei_device_keyboard_get_keymap() returns NULL on the client,
                        // causing a crash.
                        {
                            auto* ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
                            if (ctx) {
                                auto* km = xkb_keymap_new_from_names(ctx, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
                                if (km) {
                                    char* kmStr = xkb_keymap_get_as_string(km, XKB_KEYMAP_FORMAT_TEXT_V1);
                                    if (kmStr) {
                                        char tmpName[] = "/tmp/xdph-kb-XXXXXX";
                                        int kfd = mkstemp(tmpName);
                                        if (kfd >= 0) {
                                            size_t sz = strlen(kmStr);
                                            write(kfd, kmStr, sz);
                                            lseek(kfd, 0, SEEK_SET);
                                            // Use the libeis API: create keymap, add to device, release our ref
                                            auto* eisKm = eis_device_new_keymap(dev, EIS_KEYMAP_TYPE_XKB, kfd, sz);
                                            if (eisKm) {
                                                eis_keymap_add(eisKm);
                                                eis_keymap_unref(eisKm);
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
                        eis_device_add(dev);
                        eis_device_start_emulating(dev, 0);
                        Debug::log(LOG, "[remotedesktop] EIS keyboard device added & emulating");
                    }
                }
                break;
            }
            case EIS_EVENT_POINTER_MOTION: {
                if (s->virtualPointer) {
                    double dx = eis_event_pointer_get_dx(event);
                    double dy = eis_event_pointer_get_dy(event);
                    
                    s->virtualPointer->sendMotion(time,
                                                  wl_fixed_from_double(dx),
                                                  wl_fixed_from_double(dy));
                }
                break;
            }
            case EIS_EVENT_POINTER_MOTION_ABSOLUTE: {
                if (s->virtualPointer) {
                    double x = eis_event_pointer_get_absolute_x(event);
                    double y = eis_event_pointer_get_absolute_y(event);
                    
                    s->virtualPointer->sendMotionAbsolute(time, (uint32_t)x, (uint32_t)y, 3840, 2160);
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
                    int dx = eis_event_scroll_get_discrete_dx(event);
                    int dy = eis_event_scroll_get_discrete_dy(event);
                    uint32_t axis;
                    int      steps;
                    if (dy != 0) {
                        axis  = 0;
                        steps = dy;
                    } else if (dx != 0) {
                        axis  = 1;
                        steps = dx;
                    } else
                        break;
                    s->virtualPointer->sendAxisSource(2);
                    s->virtualPointer->sendAxisDiscrete(time, axis, wl_fixed_from_int(steps * 15), steps);
                }
                break;
            }
            case EIS_EVENT_KEYBOARD_KEY: {
                if (s->virtualKeyboard) {
                    uint32_t key   = eis_event_keyboard_get_key(event);
                    uint32_t state = eis_event_keyboard_get_key_is_press(event) ? 1 : 0;
                    s->virtualKeyboard->sendKey(time, key, state);
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
            default:
                break;
            }

            eis_event_unref(event);
        }
    }
}

// ─── Keysym → keycode conversion ─────────────────────────────────

uint32_t CRemoteDesktopPortal::keycodeFromKeysym(uint32_t sym, bool level0Only) {
    if (!m_xkbKeymap)
        return 0;

    xkb_keycode_t min = xkb_keymap_min_keycode(m_xkbKeymap);
    xkb_keycode_t max = xkb_keymap_max_keycode(m_xkbKeymap);

    

    int maxLevel = level0Only ? 0 : 3;
    for (xkb_keycode_t code = min; code <= max; code++) {
        for (int level = 0; level <= maxLevel; level++) {
            const xkb_keysym_t* syms;
            int nsyms = xkb_keymap_key_get_syms_by_level(m_xkbKeymap, code, 0, level, &syms);
            for (int i = 0; i < nsyms; i++) {
                if (syms[i] == static_cast<xkb_keysym_t>(sym)) {
                    // XKB keycodes are evdev scancodes + 8 in the standard evdev ruleset.
                    // code - min + 1 is wrong when min != 9; use the fixed 8 offset.
                    uint32_t evdevCode = code - 8;
                    return evdevCode;
                }
            }
        }
    }
    return 0;
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
    return 5;
}

// ─── Session lookup ──────────────────────────────────────────────

CRemoteDesktopPortal::SSession* CRemoteDesktopPortal::getSession(const sdbus::ObjectPath& path) {
    for (auto& s : m_vSessions) {
        if (s->sessionHandle == path)
            return s.get();
    }
    return nullptr;
}


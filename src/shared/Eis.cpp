#include "Eis.hpp"
#include "../core/PortalManager.hpp"
#include "../helpers/MiscFunctions.hpp"
#include "src/helpers/Log.hpp"
#include <alloca.h>
#include <libeis.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/input-event-codes.h>

EmulatedInputServer::EmulatedInputServer(std::string socketName) {
    Debug::log(LOG, "[EIS] Init socket: {}", socketName);

    const char* xdg = getenv("XDG_RUNTIME_DIR");
    if (xdg)
        socketPath = std::string(xdg) + "/" + socketName;

    if (socketPath.empty()) {
        Debug::log(ERR, "[EIS] Socket path is empty");
        return;
    }

    eisCtx = eis_new(nullptr);

    if (eis_setup_backend_socket(eisCtx, socketPath.c_str())) {
        Debug::log(ERR, "[EIS] Cannot init eis socket on {}", socketPath);
        return;
    }
    Debug::log(LOG, "[EIS] Listening on {}", socketPath);

    g_pPortalManager->addFdToEventLoop(eis_get_fd(eisCtx), POLLIN, std::bind(&EmulatedInputServer::pollEvents, this));
}

void EmulatedInputServer::pollEvents() {
    eis_dispatch(eisCtx);

    //Pull every availaible events
    while (true) {
        eis_event* e = eis_get_event(eisCtx);

        if (!e) {
            eis_event_unref(e);
            break;
        }

        int rc = onEvent(e);
        eis_event_unref(e);
        if (rc != 0)
            break;
    }
}

int EmulatedInputServer::onEvent(eis_event* e) {
    eis_client* eisClient = nullptr;
    eis_seat*   seat      = nullptr;
    eis_device* device    = nullptr;

    switch (eis_event_get_type(e)) {
        case EIS_EVENT_CLIENT_CONNECT:
            eisClient = eis_event_get_client(e);
            Debug::log(LOG, "[EIS] {} client connected: {}", eis_client_is_sender(eisClient) ? "Sender" : "Receiver", eis_client_get_name(eisClient));

            if (client.handle) {
                Debug::log(WARN, "[EIS] Unexpected additional client {} connected to input capture session", eis_client_get_name(eisClient));
                eis_client_disconnect(eisClient);
                return 0;
            }

            client.handle = eisClient;

            eis_client_connect(eisClient);
            Debug::log(LOG, "[EIS] Creating new default seat");
            seat = eis_client_new_seat(eisClient, "default");

            eis_seat_configure_capability(seat, EIS_DEVICE_CAP_POINTER);
            eis_seat_configure_capability(seat, EIS_DEVICE_CAP_POINTER_ABSOLUTE);
            eis_seat_configure_capability(seat, EIS_DEVICE_CAP_BUTTON);
            eis_seat_configure_capability(seat, EIS_DEVICE_CAP_SCROLL);
            eis_seat_configure_capability(seat, EIS_DEVICE_CAP_KEYBOARD);
            eis_seat_add(seat);
            client.seat = seat;
            break;
        case EIS_EVENT_CLIENT_DISCONNECT:
            eisClient = eis_event_get_client(e);
            Debug::log(LOG, "[EIS] {} disconnected", eis_client_get_name(eisClient));
            eis_client_disconnect(eisClient);

            eis_seat_unref(client.seat);
            clearPointer();
            clearKeyboard();
            client.handle = nullptr;
            break;
        case EIS_EVENT_SEAT_BIND:
            Debug::log(LOG, "[EIS] Binding seats...");

            if (eis_event_seat_has_capability(e, EIS_DEVICE_CAP_POINTER) && eis_event_seat_has_capability(e, EIS_DEVICE_CAP_BUTTON) &&
                eis_event_seat_has_capability(e, EIS_DEVICE_CAP_SCROLL))
                ensurePointer(e);
            else
                clearPointer();

            if (eis_event_seat_has_capability(e, EIS_DEVICE_CAP_KEYBOARD))
                ensureKeyboard(e);
            else
                clearKeyboard();
            break;
        case EIS_EVENT_DEVICE_CLOSED:
            device = eis_event_get_device(e);
            if (device == client.pointer)
                clearPointer();
            else if (device == client.keyboard) {
                Debug::log(LOG, "[EIS] Clearing keyboard");
                clearKeyboard();
            } else
                Debug::log(WARN, "[EIS] Unknown device to close");
            break;
        case EIS_EVENT_FRAME:
            if (virtualPointer != nullptr) {
                virtualPointer->sendFrame();
            }
            break;
        case EIS_EVENT_DEVICE_START_EMULATING:
            device = eis_event_get_device(e);
            Debug::log(LOG, "[EIS] Device {} is ready to send events", eis_device_get_name(device));
            break;
        case EIS_EVENT_DEVICE_STOP_EMULATING:
            device = eis_event_get_device(e);
            Debug::log(LOG, "[EIS] Device {} will no longer send events", eis_device_get_name(device));

            depressed = 0;
            virtualKeyboard->sendModifiers(depressed, 0, locked, 3);
            break;
        case EIS_EVENT_POINTER_MOTION:
            if (virtualPointer != nullptr) {
                virtualPointer->sendMotion(0, eis_event_pointer_get_dx(e), eis_event_pointer_get_dy(e));
            }
            break;
        case EIS_EVENT_POINTER_MOTION_ABSOLUTE:
            if (virtualPointer != nullptr) {
                virtualPointer->sendMotionAbsolute(0, eis_event_pointer_get_absolute_x(e), eis_event_pointer_get_absolute_y(e), screenWidth, screenHeight);
            }
            break;
        case EIS_EVENT_BUTTON_BUTTON:
            if (virtualPointer != nullptr) {
                virtualPointer->sendButton(0, eis_event_button_get_button(e), eis_event_button_get_is_press(e));
            }
            break;
        case EIS_EVENT_SCROLL_DELTA:
            if (virtualPointer != nullptr) {
                virtualPointer->sendAxis(0, 0, eis_event_scroll_get_dy(e));
                virtualPointer->sendAxis(0, 1, eis_event_scroll_get_dx(e));
            }
            break;
        case EIS_EVENT_SCROLL_STOP:
            if (virtualPointer != nullptr) {
                if (eis_event_scroll_get_stop_x(e))
                    virtualPointer->sendAxisStop(0, 1);
                if (eis_event_scroll_get_stop_y(e))
                    virtualPointer->sendAxisStop(0, 0);
            }
            break;
        case EIS_EVENT_SCROLL_DISCRETE:
            if (virtualPointer != nullptr) {
                int32_t dx = eis_event_scroll_get_discrete_dx(e);
                int32_t dy = eis_event_scroll_get_discrete_dy(e);
                virtualPointer->sendAxisDiscrete(1, 0, dy*30, 1);
                virtualPointer->sendAxisDiscrete(0, 1, dx*30, 1);
            }
            break;
        case EIS_EVENT_KEYBOARD_KEY:
            {
                if (virtualKeyboard != nullptr) {
                    uint32_t keycode = eis_event_keyboard_get_key(e);
                    bool     pressed = eis_event_keyboard_get_key_is_press(e);
                    switch (keycode) {
                        case KEY_LEFTSHIFT:
                        case KEY_RIGHTSHIFT:
                            if (pressed)
                                depressed |= 1;
                            else
                                depressed &= ~((uint32_t)1);
                            break;
                        case KEY_CAPSLOCK:
                            locked ^= ((uint32_t)1 << 4);
                            break;
                        case KEY_LEFTCTRL:
                        case KEY_RIGHTCTRL:
                            if (pressed)
                                depressed |= (uint32_t)1 << 2;
                            else
                                depressed &= ~((uint32_t)1 << 2);
                            break;
                        case KEY_LEFTALT:
                        case KEY_RIGHTALT:
                            if (pressed)
                                depressed |= (uint32_t)1 << 3;
                            else
                                depressed &= ~((uint32_t)1 << 3);
                            break;
                        case KEY_NUMLOCK:
                            if (pressed) {
                                locked ^= ((uint32_t)1 << 4);
                            }
                            break;
                        case KEY_LEFTMETA:
                        case KEY_RIGHTMETA:
                            if (pressed)
                                depressed |= (uint32_t)1 << 6;
                            else
                                depressed &= ~((uint32_t)1 << 6);
                            break;
                        case KEY_SCROLLLOCK:
                            if (pressed) {
                                locked ^= ((uint32_t)1 << 7);
                            }
                            break;
                        default:
                            break;
                    }

                    virtualKeyboard->sendModifiers(depressed, 0, locked, 3);
                    virtualKeyboard->sendKey(1, keycode, pressed);
                }
            }
            break;
       
        default: return 0;
    }
    return 0;
}

void EmulatedInputServer::ensurePointer(eis_event* event) {
    if (client.pointer)
        return;

    eis_device* pointer = eis_seat_new_device(client.seat);
    eis_device_configure_name(pointer, "captured relative pointer");
    eis_device_configure_capability(pointer, EIS_DEVICE_CAP_POINTER);
    eis_device_configure_capability(pointer, EIS_DEVICE_CAP_POINTER_ABSOLUTE);
    eis_device_configure_capability(pointer, EIS_DEVICE_CAP_BUTTON);
    eis_device_configure_capability(pointer, EIS_DEVICE_CAP_SCROLL);

    for (auto& o : g_pPortalManager->getAllOutputs()) {
        eis_region* r = eis_device_new_region(pointer);

        eis_region_set_offset(r, o->x, o->y);
        eis_region_set_size(r, o->width, o->height);
        Debug::log(LOG, "[EIS] REGION TME {} {}", o->width, o->height);
        eis_region_set_physical_scale(r, o->scale);
        eis_region_add(r);
        eis_region_unref(r);

        //#FIXME: #TODO: this doesn't work if there are multiple outputs in getAllOutPuts()
        screenWidth  = o->width;
        screenHeight = o->height;

    }

    eis_device_add(pointer);
    eis_device_resume(pointer);

    client.pointer = pointer;
}

void EmulatedInputServer::ensureKeyboard(eis_event* event) {
    if (client.keyboard)
        return;

    eis_device* keyboard = eis_seat_new_device(client.seat);
    eis_device_configure_name(keyboard, "captured keyboard");
    eis_device_configure_capability(keyboard, EIS_DEVICE_CAP_KEYBOARD);

    if (keymap.fd != 0) {
        Keymap _keymap = openKeymap();
        Debug::log(LOG, "Using keymap {}", _keymap.fd);
        eis_keymap* eis_keymap = eis_device_new_keymap(keyboard, EIS_KEYMAP_TYPE_XKB, _keymap.fd, _keymap.size);
        eis_keymap_add(eis_keymap);
        eis_keymap_unref(eis_keymap);
    }

    eis_device_add(keyboard);
    eis_device_resume(keyboard);

    client.keyboard = keyboard;
}

Keymap EmulatedInputServer::openKeymap() {
    Keymap _keymap;

    void*  src = mmap(nullptr, keymap.size, PROT_READ, MAP_PRIVATE, keymap.fd, 0);
    if (src == MAP_FAILED) {
        Debug::log(ERR, "Failed to mmap the compositor keymap fd");
        return _keymap;
    }

    int keymapFD = allocateSHMFile(keymap.size);
    if (keymapFD < 0) {
        Debug::log(ERR, "Failed to create a keymap file for keyboard grab");
        return _keymap;
    }

    char* dst = (char*)mmap(nullptr, keymap.size, PROT_READ | PROT_WRITE, MAP_SHARED, keymapFD, 0);
    if (dst == MAP_FAILED) {
        Debug::log(ERR, "Failed to mmap a keymap file for keyboard grab");
        close(keymapFD);
        return _keymap;
    }

    memcpy(dst, src, keymap.size);
    munmap(dst, keymap.size);
    munmap(src, keymap.size);

    _keymap.fd   = keymapFD;
    _keymap.size = keymap.size;

    return _keymap;
}

//TODO: remove and re-add devices when monitors change (see: mutter/meta-input-capture-session.c:1107)

void EmulatedInputServer::clearPointer() {
    if (!client.pointer)
        return;
    Debug::log(LOG, "[EIS] Clearing pointer");

    eis_device_remove(client.pointer);
    eis_device_unref(client.pointer);
    client.pointer = nullptr;
}

void EmulatedInputServer::clearKeyboard() {
    if (!client.keyboard)
        return;
    Debug::log(LOG, "[EIS] Clearing keyboard");

    eis_device_remove(client.keyboard);
    eis_device_unref(client.keyboard);
    client.keyboard = nullptr;
}

int EmulatedInputServer::getFileDescriptor() {
    return eis_backend_fd_add_client(eisCtx);
}

void EmulatedInputServer::startEmulating(int sequence) {
    Debug::log(LOG, "[EIS] Start Emulating");

    if (client.pointer)
        eis_device_start_emulating(client.pointer, sequence);

    if (client.keyboard)
        eis_device_start_emulating(client.keyboard, sequence);
}

void EmulatedInputServer::stopEmulating() {
    Debug::log(LOG, "[EIS] Stop Emulating");

    if (client.pointer)
        eis_device_stop_emulating(client.pointer);

    if (client.keyboard)
        eis_device_stop_emulating(client.keyboard);
}

void EmulatedInputServer::setKeymap(Keymap _keymap) {
    keymap = _keymap;
}

void EmulatedInputServer::sendMotion(double x, double y) {
    if (!client.pointer)
        return;
    eis_device_pointer_motion(client.pointer, x, y);
}

void EmulatedInputServer::sendKey(uint32_t key, bool pressed) {
    if (!client.keyboard)
        return;
    uint64_t now = eis_now(eisCtx);
    eis_device_keyboard_key(client.keyboard, key, pressed);
    eis_device_frame(client.keyboard, now);
}

void EmulatedInputServer::sendModifiers(uint32_t modsDepressed, uint32_t modsLatched, uint32_t modsLocked, uint32_t group) {
    if (!client.keyboard)
        return;
    uint64_t now = eis_now(eisCtx);
	eis_device_keyboard_send_xkb_modifiers(client.keyboard, modsDepressed, modsLatched, modsLocked, group);
    eis_device_frame(client.keyboard, now);
}

void EmulatedInputServer::sendButton(uint32_t button, bool pressed) {
    if (!client.pointer)
        return;
    eis_device_button_button(client.pointer, button, pressed);
}

void EmulatedInputServer::sendScrollDiscrete(int32_t x, int32_t y) {
    if (!client.pointer)
        return;
    eis_device_scroll_discrete(client.pointer, x, y);
}

void EmulatedInputServer::sendScrollDelta(double x, double y) {
    if (!client.pointer)
        return;
    eis_device_scroll_delta(client.pointer, x, y);
}

void EmulatedInputServer::sendScrollStop(bool x, bool y) {
    if (!client.pointer)
        return;
    eis_device_scroll_stop(client.pointer, x, y);
}

void EmulatedInputServer::sendPointerFrame() {
    if (!client.pointer)
        return;
    uint64_t now = eis_now(eisCtx);
    eis_device_frame(client.pointer, now);
}

void EmulatedInputServer::stopServer() {
    g_pPortalManager->removeFdFromEventLoop(eis_get_fd(eisCtx));
    Debug::log(LOG, "[EIS] Server fd {} destroyed", eis_get_fd(eisCtx));
    eis_unref(eisCtx);
    eisCtx = nullptr;
}

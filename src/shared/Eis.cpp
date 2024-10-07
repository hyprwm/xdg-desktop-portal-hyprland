#include "Eis.hpp"
#include "../core/PortalManager.hpp"
#include "../helpers/MiscFunctions.hpp"
#include "src/helpers/Log.hpp"
#include <alloca.h>
#include <libeis.h>
#include <sys/mman.h>
#include <unistd.h>

EmulatedInputServer::EmulatedInputServer(std::string socketName, Keymap _keymap) {
    Debug::log(LOG, "[EIS] Init socket: {}", socketName);

    keymap = _keymap;

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

            if (eis_client_is_sender(eisClient)) {
                Debug::log(WARN, "[EIS] Unexpected sender client {} connected to input capture session", eis_client_get_name(eisClient));
                eis_client_disconnect(eisClient);
                return 0;
            }

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
    eis_device_configure_capability(pointer, EIS_DEVICE_CAP_BUTTON);
    eis_device_configure_capability(pointer, EIS_DEVICE_CAP_SCROLL);

    for (auto& o : g_pPortalManager->getAllOutputs()) {
        eis_region* r = eis_device_new_region(pointer);

        eis_region_set_offset(r, o->x, o->y);
        eis_region_set_size(r, o->width, o->height);
        eis_region_set_physical_scale(r, o->scale);
        eis_region_add(r);
        eis_region_unref(r);
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

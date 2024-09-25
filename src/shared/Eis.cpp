#include "Eis.hpp"
#include "core/PortalManager.hpp"
#include "src/helpers/Log.hpp"
#include <libeis.h>
#include <string>
#include <sys/poll.h>
#include <thread>

EmulatedInputServer::EmulatedInputServer(std::string socketName) {
    Debug::log(LOG, "[EIS] init socket: {}", socketName);

    const char* xdg = getenv("XDG_RUNTIME_DIR");
    if (xdg)
        socketPath = std::string(xdg) + "/" + socketName;

    if (socketPath.empty()) {
        Debug::log(ERR, "[EIS] Socket path is empty");
        return;
    }

    client.handle   = NULL;
    client.seat     = NULL;
    client.pointer  = NULL;
    client.keyboard = NULL;
    eis             = eis_new(NULL);

    if (eis_setup_backend_socket(eis, socketPath.c_str())) {
        Debug::log(ERR, "[EIS] Cannot init eis socket on {}", socketPath);
        return;
    }
    Debug::log(LOG, "[EIS] Listening on {}", socketPath);

    stop = false;
    std::thread thread(&EmulatedInputServer::listen, this);
    thread.detach();
}

void EmulatedInputServer::listen() {
    struct pollfd fds = {
        .fd      = eis_get_fd(eis),
        .events  = POLLIN,
        .revents = 0,
    };
    int nevents;
    //Pull foverer events
    while (!stop && (nevents = poll(&fds, 1, 1000)) > -1) {
        eis_dispatch(eis);

        //Pull every availaible events
        while (true) {
            eis_event* e = eis_get_event(eis);

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
}

int EmulatedInputServer::onEvent(eis_event* e) {
    eis_client* client;
    eis_seat*   seat;
    eis_device* device;

    switch (eis_event_get_type(e)) {
        case EIS_EVENT_CLIENT_CONNECT:
            client = eis_event_get_client(e);
            Debug::log(LOG, "[EIS] {} client connected: {}", eis_client_is_sender(client) ? "sender" : "receiver", eis_client_get_name(client));

            if (eis_client_is_sender(client)) {
                Debug::log(WARN, "[EIS] Unexpected sender client {} connected to input capture session", eis_client_get_name(client));
                eis_client_disconnect(client);
                return 0;
            }

            if (this->client.handle != nullptr) {
                Debug::log(WARN, "[EIS] Unexpected additional client {} connected to input capture session", eis_client_get_name(client));
                eis_client_disconnect(client);
                return 0;
            }

            this->client.handle = client;

            eis_client_connect(client);
            Debug::log(LOG, "[EIS] creating new default seat");
            seat = eis_client_new_seat(client, "default");

            eis_seat_configure_capability(seat, EIS_DEVICE_CAP_POINTER);
            eis_seat_configure_capability(seat, EIS_DEVICE_CAP_BUTTON);
            eis_seat_configure_capability(seat, EIS_DEVICE_CAP_SCROLL);
            eis_seat_configure_capability(seat, EIS_DEVICE_CAP_KEYBOARD);
            eis_seat_add(seat);
            this->client.seat = seat;
            break;
        case EIS_EVENT_CLIENT_DISCONNECT:
            client = eis_event_get_client(e);
            Debug::log(LOG, "[EIS] {} disconnected", eis_client_get_name(client));
            eis_client_disconnect(client);

            eis_seat_unref(this->client.seat);
            clearPointer();
            clearKeyboard();
            this->client.handle = NULL;
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
            if (device == this->client.pointer) {
                clearPointer();
            } else if (device == this->client.keyboard) {
                Debug::log(LOG, "[EIS] Clearing keyboard");
                clearKeyboard();
            } else {
                Debug::log(WARN, "[EIS] Unknown device to close");
            }
            break;
        case EIS_EVENT_FRAME: Debug::log(LOG, "[EIS] Got event EIS_EVENT_FRAME"); break;
        case EIS_EVENT_DEVICE_START_EMULATING: Debug::log(LOG, "[EIS] Got event EIS_EVENT_DEVICE_START_EMULATING"); break;
        case EIS_EVENT_DEVICE_STOP_EMULATING: Debug::log(LOG, "[EIS] Got event EIS_EVENT_DEVICE_STOP_EMULATING"); break;
        case EIS_EVENT_POINTER_MOTION: Debug::log(LOG, "[EIS] Got event EIS_EVENT_POINTER_MOTION"); break;
        case EIS_EVENT_POINTER_MOTION_ABSOLUTE: Debug::log(LOG, "[EIS] Got event EIS_EVENT_POINTER_MOTION_ABSOLUTE"); break;
        case EIS_EVENT_BUTTON_BUTTON: Debug::log(LOG, "[EIS] Got event EIS_EVENT_BUTTON_BUTTON"); break;
        case EIS_EVENT_SCROLL_DELTA: Debug::log(LOG, "[EIS] Got event EIS_EVENT_SCROLL_DELTA"); break;
        case EIS_EVENT_SCROLL_STOP: Debug::log(LOG, "[EIS] Got event EIS_EVENT_SCROLL_STOP"); break;
        case EIS_EVENT_SCROLL_CANCEL: Debug::log(LOG, "[EIS] Got event EIS_EVENT_SCROLL_CANCEL"); break;
        case EIS_EVENT_SCROLL_DISCRETE: Debug::log(LOG, "[EIS] Got event EIS_EVENT_SCROLL_DISCRETE"); break;
        case EIS_EVENT_KEYBOARD_KEY: Debug::log(LOG, "[EIS] Got event EIS_EVENT_KEYBOARD_KEY"); break;
        case EIS_EVENT_TOUCH_DOWN: Debug::log(LOG, "[EIS] Got event EIS_EVENT_TOUCH_DOWN"); break;
        case EIS_EVENT_TOUCH_UP: Debug::log(LOG, "[EIS] Got event EIS_EVENT_TOUCH_UP"); break;
        case EIS_EVENT_TOUCH_MOTION: Debug::log(LOG, "[EIS] Got event EIS_EVENT_TOUCH_MOTION"); break;
    }
    return 0;
}

void EmulatedInputServer::ensurePointer(eis_event* event) {
    if (client.pointer != nullptr)
        return;

    struct eis_device* pointer = eis_seat_new_device(client.seat);
    eis_device_configure_name(pointer, "captured relative pointer");
    eis_device_configure_capability(pointer, EIS_DEVICE_CAP_POINTER);
    eis_device_configure_capability(pointer, EIS_DEVICE_CAP_BUTTON);
    eis_device_configure_capability(pointer, EIS_DEVICE_CAP_SCROLL);

    for (auto& o : g_pPortalManager->getAllOutputs()) {
        struct eis_region* r = eis_device_new_region(pointer);

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
    if (client.keyboard != nullptr)
        return;

    struct eis_device* keyboard = eis_seat_new_device(client.seat);
    eis_device_configure_name(keyboard, "captured keyboard");
    eis_device_configure_capability(keyboard, EIS_DEVICE_CAP_KEYBOARD);
    // TODO: layout
    eis_device_add(keyboard);
    eis_device_resume(keyboard);

    client.keyboard = keyboard;
}

//TODO: remove and re-add devices when monitors change (see: mutter/meta-input-capture-session.c:1107)

void EmulatedInputServer::clearPointer() {
    if (client.pointer == nullptr)
        return;
    Debug::log(LOG, "[EIS] Clearing pointer");

    eis_device_remove(client.pointer);
    eis_device_unref(client.pointer);
    client.pointer = nullptr;
}

void EmulatedInputServer::clearKeyboard() {
    if (client.keyboard == nullptr)
        return;
    Debug::log(LOG, "[EIS] Clearing keyboard");

    eis_device_remove(client.keyboard);
    eis_device_unref(client.keyboard);
    client.keyboard = nullptr;
}

int EmulatedInputServer::getFileDescriptor() {
    return eis_backend_fd_add_client(eis);
}

void EmulatedInputServer::startEmulating(int sequence) {
    Debug::log(LOG, "[EIS] Start Emulating");

    if (client.pointer != nullptr)
        eis_device_start_emulating(client.pointer, sequence);

    if (client.keyboard != nullptr)
        eis_device_start_emulating(client.keyboard, sequence);
}

void EmulatedInputServer::stopEmulating() {
    Debug::log(LOG, "[EIS] Stop Emulating");

    if (client.pointer != nullptr)
        eis_device_stop_emulating(client.pointer);

    if (client.keyboard != nullptr)
        eis_device_stop_emulating(client.keyboard);
}

void EmulatedInputServer::sendMotion(double x, double y) {
    if (client.pointer == nullptr)
        return;
    eis_device_pointer_motion(client.pointer, x, y);
}

void EmulatedInputServer::sendKey(uint32_t key, bool pressed) {
    if (client.keyboard == nullptr)
        return;
    uint64_t now = eis_now(eis);
    eis_device_keyboard_key(client.keyboard, key, pressed);
    eis_device_frame(client.keyboard, now);
}

void EmulatedInputServer::sendButton(uint32_t button, bool pressed) {
    if (client.pointer == nullptr)
        return;
    eis_device_button_button(client.pointer, button, pressed);
}

void EmulatedInputServer::sendScrollDiscrete(int32_t x, int32_t y) {
    if (client.pointer == nullptr)
        return;
    eis_device_scroll_discrete(client.pointer, x, y);
}

void EmulatedInputServer::sendScrollDelta(double x, double y) {
    if (client.pointer == nullptr)
        return;
    eis_device_scroll_delta(client.pointer, x, y);
}

void EmulatedInputServer::sendScrollStop(bool x, bool y) {
    if (client.pointer == nullptr)
        return;
    eis_device_scroll_stop(client.pointer, x, y);
}

void EmulatedInputServer::sendPointerFrame() {
    if (client.pointer == nullptr)
        return;
    uint64_t now = eis_now(eis);
    eis_device_frame(client.pointer, now);
}

void EmulatedInputServer::stopServer() {
    stop = true;
}

#pragma once

#include <libei-1.0/libeis.h>
#include <string>
#include "wlr-virtual-pointer-unstable-v1.hpp"
#include "virtual-keyboard-unstable-v1.hpp"
#include <memory>
#include "../includes.hpp"

struct Keymap {
    int32_t  fd   = 0;
    uint32_t size = 0;
};

/*
 * Responsible to creating a socket for input communication
 */
class EmulatedInputServer {
  public:
    EmulatedInputServer(std::string socketPath);
    std::string socketPath;

    void        startEmulating(int activationId);
    void        stopEmulating();

    void        setKeymap(Keymap _keymap);

    void        sendMotion(double x, double y);
    void        sendKey(uint32_t key, bool pressed);
    void        sendModifiers(uint32_t modsDepressed, uint32_t modsLatched, uint32_t modsLocked, uint32_t group);
    void        sendButton(uint32_t button, bool pressed);
    void        sendScrollDelta(double x, double y);
    void        sendScrollDiscrete(int32_t x, int32_t y);
    void        sendScrollStop(bool stopX, bool stopY);
    void        sendPointerFrame();

    int         getFileDescriptor();

    void        setVirtualPointer(SP<CCZwlrVirtualPointerV1> ptr) {virtualPointer  = ptr;}
    void        setVirtualKeyboard(SP<CCZwpVirtualKeyboardV1> kb) {virtualKeyboard = kb; }

    void        stopServer();

  private:
    bool stop   = false;
    eis* eisCtx = nullptr;

    struct Client {
        eis_client* handle = nullptr;
        eis_seat*   seat   = nullptr;

        eis_device* pointer  = nullptr;
        eis_device* keyboard = nullptr;
    } client;

    SP<CCZwlrVirtualPointerV1> virtualPointer  = nullptr;
    SP<CCZwpVirtualKeyboardV1> virtualKeyboard = nullptr;
    uint32_t                   screenWidth     = 0;
    uint32_t                   screenHeight    = 0;

    uint32_t                   depressed       = 0;
    uint32_t                   latched         = 0;
    uint32_t                   locked          = 0;

    Keymap keymap;

    int    onEvent(eis_event* e);
    void   pollEvents();
    void   ensurePointer(eis_event* event);
    void   ensureKeyboard(eis_event* event);
    Keymap openKeymap();
    void   clearPointer();
    void   clearKeyboard();
};

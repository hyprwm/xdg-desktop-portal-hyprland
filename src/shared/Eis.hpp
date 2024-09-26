#pragma once

#include <libeis.h>
#include <string>

struct EisClient {
    struct eis_client* handle;
    struct eis_seat*   seat;

    struct eis_device* pointer;
    struct eis_device* keyboard;
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

    void        sendMotion(double x, double y);
    void        sendKey(uint32_t key, bool pressed);
    void        sendButton(uint32_t button, bool pressed);
    void        sendScrollDelta(double x, double y);
    void        sendScrollDiscrete(int32_t x, int32_t y);
    void        sendScrollStop(bool stopX, bool stopY);
    void        sendPointerFrame();

    int         getFileDescriptor();

    void        stopServer();

  private:
    bool        stop;
    struct eis* eis;
    EisClient   client;

    int         onEvent(eis_event* e);
    void        listen();
    void        ensurePointer(eis_event* event);
    void        ensureKeyboard(eis_event* event);
    void        clearPointer();
    void        clearKeyboard();
};

#pragma once
#include "hyprland-input-capture-v1.hpp"
#include "shared/Eis.hpp"
#include <cstdint>
#include <sdbus-c++/Types.h>
#include <sdbus-c++/sdbus-c++.h>
#include <string>
#include <unordered_map>
#include <wayland-client-protocol.h>
#include "../includes.hpp"
#include "../shared/Session.hpp"

typedef int        ClientStatus;
const ClientStatus CREATED   = 0; //Is ready to be activated
const ClientStatus ENABLED   = 1; //Is ready for receiving inputs
const ClientStatus ACTIVATED = 2; //Currently receiving inputs
const ClientStatus STOPPED   = 3; //Can no longer be activated

struct Barrier {
    uint id;
    int  x1, y1, x2, y2;
};

class CInputCapturePortal {
  public:
    CInputCapturePortal(SP<CCHyprlandInputCaptureManagerV1> mgr);

    void onCreateSession(sdbus::MethodCall& methodCall);
    void onGetZones(sdbus::MethodCall& methodCall);
    void onSetPointerBarriers(sdbus::MethodCall& methodCall);
    void onEnable(sdbus::MethodCall& methodCall);
    void onDisable(sdbus::MethodCall& methodCall);
    void onRelease(sdbus::MethodCall& methodCall);
    void onConnectToEIS(sdbus::MethodCall& methodCall);

    void onAbsoluteMotion(double x, double y, double dx, double dy);
    void onKey(uint32_t key, bool pressed);
    void onButton(uint32_t button, bool pressed);
    void onAxis(bool axis, double value);
    void onAxisValue120(bool axis, int32_t value120);
    void onAxisStop(bool axis);
    void onFrame();

    void zonesChanged();

    struct Session {
        std::string                           appid;
        sdbus::ObjectPath                     requestHandle, sessionHandle;
        std::string                           sessionId;
        uint32_t                              capabilities;

        std::unique_ptr<SDBusRequest>         request;
        std::unique_ptr<SDBusSession>         session;
        std::unique_ptr<EmulatedInputServer>  eis;

        std::unordered_map<uint32_t, Barrier> barriers;
        uint32_t                              activationId;
        ClientStatus                          status;

        //
        bool     activate(double x, double y, uint32_t borderId);
        bool     deactivate();
        bool     disable();
        bool     zoneChanged();

        void     motion(double dx, double dy);
        void     key(uint32_t key, bool pressed);
        void     button(uint32_t button, bool pressed);
        void     axis(bool axis, double value);
        void     axisValue120(bool axis, int32_t value120);
        void     axisStop(bool axis);
        void     frame();

        uint32_t isColliding(double px, double py, double nx, double ny);
    };

  private:
    struct {
        SP<CCHyprlandInputCaptureManagerV1> manager;
    } m_sState;

    std::unordered_map<std::string, const std::shared_ptr<Session>> sessions;
    //
    std::unique_ptr<sdbus::IObject> m_pObject;
    uint                            sessionCounter;
    uint                            lastZoneSet;

    const std::string               INTERFACE_NAME = "org.freedesktop.impl.portal.InputCapture";
    const std::string               OBJECT_PATH    = "/org/freedesktop/portal/desktop";

    bool                            sessionValid(sdbus::ObjectPath sessionHandle);

    void                            activate(sdbus::ObjectPath sessionHandle, double x, double y, uint32_t borderId);
    void                            deactivate(sdbus::ObjectPath sessionHandle);
    void                            disable(sdbus::ObjectPath sessionHandle);
};

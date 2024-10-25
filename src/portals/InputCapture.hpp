#pragma once
#include "dbusDefines.hpp"
#include "hyprland-input-capture-v1.hpp"
#include "../shared/Eis.hpp"
#include "../includes.hpp"
#include "../shared/Session.hpp"
#include <sdbus-c++/Types.h>

enum ClientStatus {
    CREATED,   //Is ready to be activated
    ENABLED,   //Is ready for receiving inputs
    ACTIVATED, //Currently receiving inputs
    STOPPED    //Can no longer be activated
};

struct SBarrier {
    uint id;
    int  x1, y1, x2, y2;
};

class CInputCapturePortal {
  public:
    CInputCapturePortal(SP<CCHyprlandInputCaptureManagerV1> mgr);

    dbUasv        onCreateSession(sdbus::ObjectPath requestHandle, sdbus::ObjectPath sessionHandle, std::string appID, std::string parentWindow,
                                  std::unordered_map<std::string, sdbus::Variant> options);
    dbUasv        onGetZones(sdbus::ObjectPath requestHandle, sdbus::ObjectPath sessionHandle, std::string appID, std::unordered_map<std::string, sdbus::Variant> opts);
    dbUasv        onSetPointerBarriers(sdbus::ObjectPath requestHandle, sdbus::ObjectPath sessionHandle, std::string appID, std::unordered_map<std::string, sdbus::Variant> opts,
                                       std::vector<std::unordered_map<std::string, sdbus::Variant>> barriers, uint32_t zoneSet);
    dbUasv        onEnable(sdbus::ObjectPath sessionHandle, std::string appID, std::unordered_map<std::string, sdbus::Variant> opts);
    dbUasv        onDisable(sdbus::ObjectPath sessionHandle, std::string appID, std::unordered_map<std::string, sdbus::Variant> opts);
    dbUasv        onRelease(sdbus::ObjectPath sessionHandle, std::string appID, std::unordered_map<std::string, sdbus::Variant> opts);
    sdbus::UnixFd onConnectToEIS(sdbus::ObjectPath sessionHandle, std::string appID, std::unordered_map<std::string, sdbus::Variant> opts);

    void          onForceRelease();
    void          onMotion(double x, double y, double dx, double dy);
    void          onKeymap(int32_t fd, uint32_t size);
    void          onKey(uint32_t key, bool pressed);
    void          onButton(uint32_t button, bool pressed);
    void          onAxis(bool axis, double value);
    void          onAxisValue120(bool axis, int32_t value120);
    void          onAxisStop(bool axis);
    void          onFrame();

    void          zonesChanged();

    struct SSession {
        std::string                            appid;
        sdbus::ObjectPath                      requestHandle, sessionHandle;
        std::string                            sessionId;
        uint32_t                               capabilities = 0;

        std::unique_ptr<SDBusRequest>          request;
        std::unique_ptr<SDBusSession>          session;
        std::unique_ptr<EmulatedInputServer>   eis;

        std::unordered_map<uint32_t, SBarrier> barriers;
        uint32_t                               activationId = 0;
        ClientStatus                           status       = CREATED;

        //
        bool     activate(double x, double y, uint32_t borderId);
        bool     deactivate();
        bool     disable();
        bool     zoneChanged();

        void     motion(double dx, double dy);
        void     key(uint32_t key, bool pressed);
        void     keymap(Keymap keymap);
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

    std::unordered_map<std::string, const std::shared_ptr<SSession>> sessions;
    //
    std::unique_ptr<sdbus::IObject> m_pObject;
    uint                            sessionCounter = 0;
    uint                            lastZoneSet    = 0;

    Keymap                          keymap; //We store the active keymap ready to be sent when creating EIS

    const sdbus::InterfaceName      INTERFACE_NAME = sdbus::InterfaceName{"org.freedesktop.impl.portal.InputCapture"};
    const sdbus::ObjectPath         OBJECT_PATH    = sdbus::ObjectPath{"/org/freedesktop/portal/desktop"};

    bool                            sessionValid(sdbus::ObjectPath sessionHandle);

    void                            activate(sdbus::ObjectPath sessionHandle, double x, double y, uint32_t borderId);
    void                            deactivate(sdbus::ObjectPath sessionHandle);
    void                            disable(sdbus::ObjectPath sessionHandle);
};

#pragma once
#include "../dbusDefines.hpp"
#include "hyprland-input-capture-v1.hpp"
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

const static sdbus::InterfaceName INTERFACE_NAME = sdbus::InterfaceName{"org.freedesktop.impl.portal.InputCapture"};
const static sdbus::ObjectPath    OBJECT_PATH    = sdbus::ObjectPath{"/org/freedesktop/portal/desktop"};

class CInputCapturePortal {
  public:
    CInputCapturePortal(SP<CCHyprlandInputCaptureManagerV1> mgr);
    void zonesChanged();

  private:
    struct {
        SP<CCHyprlandInputCaptureManagerV1> manager;
    } m_sState;

    struct SSession {
        SSession(sdbus::ObjectPath requestHandle, sdbus::ObjectPath sessionHandle, std::string sessionId, uint32_t capabilities, wl_proxy* proxy);

        sdbus::ObjectPath requestHandle, sessionHandle;
        std::string       sessionId;
        uint32_t          capabilities = 0;
        uint32_t          eisFD        = 0;
        bool              dead         = false;

        //
        std::unique_ptr<SDBusRequest>             request;
        std::unique_ptr<SDBusSession>             session;
        std::unique_ptr<CCHyprlandInputCaptureV1> whandle;
    };

    std::unordered_map<std::string, const std::shared_ptr<SSession>> sessions;
    //
    std::unique_ptr<sdbus::IObject> m_pObject;
    uint                            sessionCounter = 0;
    uint                            lastZoneSet    = 0;

    //
    dbUasv        onCreateSession(sdbus::ObjectPath requestHandle, sdbus::ObjectPath sessionHandle, std::string appID, std::string parentWindow,
                                  std::unordered_map<std::string, sdbus::Variant> options);
    dbUasv        onGetZones(sdbus::ObjectPath requestHandle, sdbus::ObjectPath sessionHandle, std::string appID, std::unordered_map<std::string, sdbus::Variant> opts);
    dbUasv        onSetPointerBarriers(sdbus::ObjectPath requestHandle, sdbus::ObjectPath sessionHandle, std::string appID, std::unordered_map<std::string, sdbus::Variant> opts,
                                       std::vector<std::unordered_map<std::string, sdbus::Variant>> barriers, uint32_t zoneSet);
    dbUasv        onEnable(sdbus::ObjectPath sessionHandle, std::string appID, std::unordered_map<std::string, sdbus::Variant> opts);
    dbUasv        onDisable(sdbus::ObjectPath sessionHandle, std::string appID, std::unordered_map<std::string, sdbus::Variant> opts);
    dbUasv        onRelease(sdbus::ObjectPath sessionHandle, std::string appID, std::unordered_map<std::string, sdbus::Variant> opts);
    sdbus::UnixFd onConnectToEIS(sdbus::ObjectPath sessionHandle, std::string appID, std::unordered_map<std::string, sdbus::Variant> opts);

    bool          sessionValid(sdbus::ObjectPath sessionHandle);
    void          removeSession(sdbus::ObjectPath sessionHandle);

    void          activate(sdbus::ObjectPath sessionHandle, uint32_t activationId, double x, double y, uint32_t borderId);
    void          deactivate(sdbus::ObjectPath sessionHandle, uint32_t activationId);
    void          disable(sdbus::ObjectPath sessionHandle);
};

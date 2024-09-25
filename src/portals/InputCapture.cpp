#include "InputCapture.hpp"

#include "../core/PortalManager.hpp"
#include "../helpers/Log.hpp"
#include "../shared/Session.hpp"
#include "hyprland-input-capture-v1.hpp"
#include <cstdint>
#include <hyprutils/memory/UniquePtr.hpp>
#include <memory>
#include <sdbus-c++/Types.h>
#include <sdbus-c++/VTableItems.h>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unordered_map>
#include <wayland-client-core.h>
#include <wayland-util.h>

CInputCapturePortal::CInputCapturePortal(SP<CCHyprlandInputCaptureManagerV1> mgr) : m_sState(mgr) {
    Debug::log(LOG, "[input-capture] initializing input capture portal");

    m_pObject = sdbus::createObject(*g_pPortalManager->getConnection(), OBJECT_PATH);

    m_pObject
        ->addVTable(
            sdbus::registerMethod("CreateSession")
                .implementedAs([this](sdbus::ObjectPath o1, sdbus::ObjectPath o2, std::string s1, std::string s2, std::unordered_map<std::string, sdbus::Variant> m) {
                    return onCreateSession(o1, o2, s1, s2, m);
                }),
            sdbus::registerMethod("GetZones").implementedAs([this](sdbus::ObjectPath o1, sdbus::ObjectPath o2, std::string s, std::unordered_map<std::string, sdbus::Variant> m) {
                return onGetZones(o1, o2, s, m);
            }),
            sdbus::registerMethod("SetPointerBarriers")
                .implementedAs([this](sdbus::ObjectPath o1, sdbus::ObjectPath o2, std::string s, std::unordered_map<std::string, sdbus::Variant> m,
                                      std::vector<std::unordered_map<std::string, sdbus::Variant>> v, uint32_t u) { return onSetPointerBarriers(o1, o2, s, m, v, u); }),
            sdbus::registerMethod("Enable").implementedAs(
                [this](sdbus::ObjectPath o, std::string s, std::unordered_map<std::string, sdbus::Variant> m) { return onEnable(o, s, m); }),

            sdbus::registerMethod("Disable").implementedAs(
                [this](sdbus::ObjectPath o, std::string s, std::unordered_map<std::string, sdbus::Variant> m) { return onDisable(o, s, m); }),

            sdbus::registerMethod("Release").implementedAs(
                [this](sdbus::ObjectPath o, std::string s, std::unordered_map<std::string, sdbus::Variant> m) { return onRelease(o, s, m); }),
            sdbus::registerMethod("ConnectToEIS").implementedAs([this](sdbus::ObjectPath o, std::string s, std::unordered_map<std::string, sdbus::Variant> m) {
                return onConnectToEIS(o, s, m);
            }),
            sdbus::registerProperty("SupportedCapabilities").withGetter([] { return (uint32_t)(1 | 2); }),
            sdbus::registerProperty("version").withGetter([] { return (uint32_t)(1); }),
            sdbus::registerSignal("Activated").withParameters<sdbus::ObjectPath, std::unordered_map<std::string, sdbus::Variant>>(),
            sdbus::registerSignal("Disabled").withParameters<sdbus::ObjectPath, std::unordered_map<std::string, sdbus::Variant>>(),
            sdbus::registerSignal("Deactivated").withParameters<sdbus::ObjectPath, std::unordered_map<std::string, sdbus::Variant>>(),
            sdbus::registerSignal("ZonesChanged").withParameters<sdbus::ObjectPath, std::unordered_map<std::string, sdbus::Variant>>())
        .forInterface(INTERFACE_NAME);

    for (auto& o : g_pPortalManager->getAllOutputs())
        Debug::log(LOG, "{} {}x{}", o->name, o->width, o->height);

    Debug::log(LOG, "[input-capture] init successful");
}

void complete(sdbus::MethodCall& call) {
    auto reply = call.createReply();
    reply << (uint32_t)0;
    reply << std::unordered_map<std::string, sdbus::Variant>{};
    reply.send();
}

dbUasv CInputCapturePortal::onCreateSession(sdbus::ObjectPath requestHandle, sdbus::ObjectPath sessionHandle, std::string appID, std::string parentWindow,
                                            std::unordered_map<std::string, sdbus::Variant> options) {
    Debug::log(LOG, "[input-capture] New session:");

    uint32_t capabilities = options["capabilities"].get<uint32_t>();

    Debug::log(LOG, "[input-capture]  | {}", requestHandle.c_str());
    Debug::log(LOG, "[input-capture]  | {}", sessionHandle.c_str());
    Debug::log(LOG, "[input-capture]  | appid: {}", appID);
    Debug::log(LOG, "[input-capture]  | parent_window: {}", parentWindow);
    Debug::log(LOG, "[input-capture]  | capabilities : {}", capabilities);

    std::string sessionId = "input-capture-" + std::to_string(sessionCounter++);
    Debug::log(LOG, "[input-capture]  | sessionId : {}", sessionId);

    const std::shared_ptr<SSession> session =
        std::make_shared<SSession>(requestHandle, sessionHandle, sessionId, capabilities, m_sState.manager->sendCreateSession(sessionId.c_str()));

    sessions.emplace(sessionHandle, session);

    std::unordered_map<std::string, sdbus::Variant> results;
    results["capabilities"] = sdbus::Variant{(uint)3};
    results["session_id"]   = sdbus::Variant{sessionId};
    return {0, results};
}

CInputCapturePortal::SSession::SSession(sdbus::ObjectPath requestHandle_, sdbus::ObjectPath sessionHandle_, std::string sessionId_, uint32_t capabilities_, wl_proxy* proxy) :
    requestHandle(requestHandle_), sessionHandle(sessionHandle_), sessionId(sessionId_), capabilities(capabilities_), whandle(std::make_unique<CCHyprlandInputCaptureV1>(proxy)) {

    session            = createDBusSession(sessionHandle);
    session->onDestroy = [this]() {
        whandle->sendDisable();
        Debug::log(LOG, "[input-capture] Session {} destroyed", sessionHandle.c_str());
        session.release();
        whandle.reset();
        dead = true;
    };

    request            = createDBusRequest(requestHandle);
    request->onDestroy = [this]() { request.release(); };

    whandle->setActivated([this](CCHyprlandInputCaptureV1*, uint32_t activationId, wl_fixed_t x, wl_fixed_t y, uint32_t borderId) {
        g_pPortalManager->m_sPortals.inputCapture->activate(sessionHandle, activationId, x, y, borderId);
    });
    whandle->setDeactivated([this](CCHyprlandInputCaptureV1*, uint32_t activationId) { g_pPortalManager->m_sPortals.inputCapture->deactivate(sessionHandle, activationId); });
    whandle->setDisabled([this](CCHyprlandInputCaptureV1*) { g_pPortalManager->m_sPortals.inputCapture->disable(sessionHandle); });
    whandle->setEisFd([this](CCHyprlandInputCaptureV1*, int32_t fd) { eisFD = fd; });
}

dbUasv CInputCapturePortal::onGetZones(sdbus::ObjectPath requestHandle, sdbus::ObjectPath sessionHandle, std::string appID, std::unordered_map<std::string, sdbus::Variant> opts) {
    Debug::log(LOG, "[input-capture] New GetZones request:");
    Debug::log(LOG, "[input-capture]  | {}", requestHandle.c_str());
    Debug::log(LOG, "[input-capture]  | {}", sessionHandle.c_str());
    Debug::log(LOG, "[input-capture]  | appid: {}", appID);

    if (!sessionValid(sessionHandle))
        return {1, {}};

    std::vector<sdbus::Struct<uint32_t, uint32_t, int32_t, int32_t>> zones;
    for (auto& o : g_pPortalManager->getAllOutputs()) {
        Debug::log(LOG, "[input-capture]  | w: {} h: {} x: {} y: {}", o->width, o->height, o->x, o->y);
        zones.push_back(sdbus::Struct(o->width, o->height, o->x, o->y));
    }

    std::unordered_map<std::string, sdbus::Variant> results;
    results["zones"]    = sdbus::Variant{zones};
    results["zone_set"] = sdbus::Variant{++lastZoneSet};
    return {0, results};
}

enum ValidResult {
    Error,
    Invalid,
    Partial,
    Valid
};

ValidResult isBarrierValidAgainstMonitor(int x1, int y1, int x2, int y2, const std::unique_ptr<SOutput>& monitor) {
    int mx1 = monitor->x;
    int my1 = monitor->y;
    int mx2 = mx1 + monitor->width - 1;
    int my2 = my1 + monitor->height - 1;
    if (monitor->width == 0 || monitor->height == 0) //If we have an invalid monitor we refuse all barriers
        return Error;

    if (x1 == x2) {                     //If zone is vertical
        if (x1 != mx1 && x1 != mx2 + 1) //If the zone don't touch the left or right side
            return Invalid;

        if (y1 != my1 || y2 != my2) {                                 //If the zone is shorter than the height of the screen
            if ((my1 <= y1 && y1 <= my2) || (my1 <= y2 && y2 <= my2)) //Maybe the segments are overlapping
                return Partial;
            return Invalid;
        }
    } else {
        if (y1 != my1 && y1 != my2 + 1) //If the zone don't touch the bottom or top side
            return Invalid;
        if (x1 != mx1 || x2 != mx2) {                                 //If the zone is shorter than the height of the screen
            if ((mx1 <= x1 && x1 <= mx2) || (mx1 <= x2 && x2 <= mx2)) //Maybe the segments are overlapping
                return Partial;
            return Invalid;
        }
    }

    return Valid;
}

bool isBarrierValid(int x1, int y1, int x2, int y2) {
    if (x1 != x2 && y1 != y2) //At least one axis should be aligned
        return false;

    if (x1 == x2 && y1 == y2) //The barrier should have non-null area
        return false;

    if (x1 > x2)
        std::swap(x1, x2);

    if (y1 > y2)
        std::swap(y1, y2);

    int valid   = 0;
    int partial = 0; //Used to detect if a barrier is placed on the side of two monitors
    for (auto& o : g_pPortalManager->getAllOutputs())
        switch (isBarrierValidAgainstMonitor(x1, y1, x2, y2, o)) {
            case Valid: valid++; break;
            case Partial: partial++; break;
            case Invalid: break;
            case Error: return false;
        }

    return valid == 1 && partial == 0;
}

dbUasv CInputCapturePortal::onSetPointerBarriers(sdbus::ObjectPath requestHandle, sdbus::ObjectPath sessionHandle, std::string appID,
                                                 std::unordered_map<std::string, sdbus::Variant> opts, std::vector<std::unordered_map<std::string, sdbus::Variant>> barriers,
                                                 uint32_t zoneSet) {
    Debug::log(LOG, "[input-capture] New SetPointerBarriers request:");

    Debug::log(LOG, "[input-capture]  | {}", requestHandle.c_str());
    Debug::log(LOG, "[input-capture]  | {}", sessionHandle.c_str());
    Debug::log(LOG, "[input-capture]  | appid: {}", appID);

    if (!sessionValid(sessionHandle))
        return {1, {}};

    Debug::log(LOG, "[input-capture]  | zoneSet: {}", zoneSet);

    if (zoneSet != lastZoneSet) {
        Debug::log(WARN, "[input-capture] Invalid zone set, discarding barriers");
        return {0, {}}; //TODO: We should return failed_barries
    }

    std::vector<uint> failedBarriers;
    sessions[sessionHandle]->whandle->sendClearBarriers();
    for (const auto& b : barriers) {
        uint                              id = b.at("barrier_id").get<uint>();
        int                               x1, y1, x2, y2;

        sdbus::Struct<int, int, int, int> p = b.at("position").get<sdbus::Struct<int, int, int, int>>();
        x1                                  = p.get<0>();
        y1                                  = p.get<1>();
        x2                                  = p.get<2>();
        y2                                  = p.get<3>();

        bool valid = isBarrierValid(x1, y1, x2, y2);
        Debug::log(LOG, "[input-capture]  | barrier: {}, [{}, {}] [{}, {}] valid: {}", id, x1, y1, x2, y2, valid);
        if (valid)
            sessions[sessionHandle]->whandle->sendAddBarrier(zoneSet, id, x1, y1, x2, y2);
        else
            failedBarriers.push_back(id);
    }

    std::unordered_map<std::string, sdbus::Variant> results;
    results["failed_barriers"] = sdbus::Variant{failedBarriers};
    return {0, results};
}

dbUasv CInputCapturePortal::onDisable(sdbus::ObjectPath sessionHandle, std::string appID, std::unordered_map<std::string, sdbus::Variant> opts) {
    Debug::log(LOG, "[input-capture] New Disable request:");
    Debug::log(LOG, "[input-capture]  | {}", sessionHandle.c_str());
    Debug::log(LOG, "[input-capture]  | appid: {}", appID);

    if (!sessionValid(sessionHandle))
        return {1, {}};

    sessions[sessionHandle]->whandle->sendDisable();
    return {0, {}};
}

dbUasv CInputCapturePortal::onEnable(sdbus::ObjectPath sessionHandle, std::string appID, std::unordered_map<std::string, sdbus::Variant> opts) {
    Debug::log(LOG, "[input-capture] New Enable request:");
    Debug::log(LOG, "[input-capture]  | {}", sessionHandle.c_str());
    Debug::log(LOG, "[input-capture]  | appid: {}", appID);

    if (!sessionValid(sessionHandle)) {
        Debug::log(WARN, "[input-capture] Unknown session handle: {}", sessionHandle.c_str());
        return {1, {}};
    }

    sessions[sessionHandle]->whandle->sendEnable();
    return {0, {}};
}

dbUasv CInputCapturePortal::onRelease(sdbus::ObjectPath sessionHandle, std::string appID, std::unordered_map<std::string, sdbus::Variant> opts) {
    Debug::log(LOG, "[input-capture] New Release request:");
    Debug::log(LOG, "[input-capture]  | {}", sessionHandle.c_str());
    Debug::log(LOG, "[input-capture]  | appid: {}", appID);

    if (!sessionValid(sessionHandle))
        return {1, {}};

    uint32_t activationId = opts["activation_id"].get<uint32_t>();
    Debug::log(LOG, "[input-capture]  | activationId: {}", activationId);

    double x = -1;
    double y = -1;
    if (opts.contains("cursor_position")) {
        auto [px, py] = opts["cursor_position"].get<sdbus::Struct<double, double>>();
        Debug::log(LOG, "[input-capture]  | cursorPosition: {} {}", px, py);
        x = px;
        y = py;
    }
    sessions[sessionHandle]->whandle->sendRelease(activationId, x, y); //TODO: send pointer position for warping

    return {0, {}};
}

sdbus::UnixFd CInputCapturePortal::onConnectToEIS(sdbus::ObjectPath sessionHandle, std::string appID, std::unordered_map<std::string, sdbus::Variant> opts) {
    Debug::log(LOG, "[input-capture] New ConnectToEIS request:");
    Debug::log(LOG, "[input-capture]  | {}", sessionHandle.c_str());
    Debug::log(LOG, "[input-capture]  | appid: {}", appID);

    if (!sessionValid(sessionHandle))
        return (sdbus::UnixFd)0;

    return (sdbus::UnixFd)sessions[sessionHandle]->eisFD;
}

bool CInputCapturePortal::sessionValid(sdbus::ObjectPath sessionHandle) {
    if (!sessions.contains(sessionHandle)) {
        Debug::log(WARN, "[input-capture] Unknown session handle: {}", sessionHandle.c_str());
        return false;
    }

    return !sessions[sessionHandle]->dead;
}

void CInputCapturePortal::removeSession(sdbus::ObjectPath sessionHandle) {
    sessions.erase(sessionHandle);
}

void CInputCapturePortal::zonesChanged() {
    if (sessions.empty())
        return;

    Debug::log(LOG, "[input-capture] Monitor layout has changed, notifing clients");
    lastZoneSet++;

    for (auto& [key, value] : sessions) {
        if (!sessionValid(value->sessionHandle))
            continue;

        std::unordered_map<std::string, sdbus::Variant> options;
        options["zone_set"] = sdbus::Variant{lastZoneSet - 1};
        m_pObject->emitSignal("ZonesChanged").onInterface(INTERFACE_NAME).withArguments(value->sessionHandle, options);
    }
}

void CInputCapturePortal::activate(sdbus::ObjectPath sessionHandle, uint32_t activationId, double x, double y, uint32_t borderId) {
    Debug::log(LOG, "[input-capture] activate, activationId {}, barrierId {}, x {}, y {}", activationId, borderId, x, y);
    std::unordered_map<std::string, sdbus::Variant> results;
    results["activation_id"]   = sdbus::Variant{activationId};
    results["cursor_position"] = sdbus::Variant{sdbus::Struct<double, double>(x, y)};
    results["barrier_id"]      = sdbus::Variant{borderId};

    m_pObject->emitSignal("Activated").onInterface(INTERFACE_NAME).withArguments(sessionHandle, results);
}

void CInputCapturePortal::deactivate(sdbus::ObjectPath sessionHandle, uint32_t activationId) {
    std::unordered_map<std::string, sdbus::Variant> options;
    options["activation_id"] = sdbus::Variant{activationId};
    m_pObject->emitSignal("Deactivated").onInterface(INTERFACE_NAME).withArguments(sessionHandle, options);
}

void CInputCapturePortal::disable(sdbus::ObjectPath sessionHandle) {
    std::unordered_map<std::string, sdbus::Variant> options;
    m_pObject->emitSignal("Disabled").onInterface(INTERFACE_NAME).withArguments(sessionHandle, options);
}

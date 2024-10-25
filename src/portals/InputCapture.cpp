#include "InputCapture.hpp"

#include "../core/PortalManager.hpp"
#include "../helpers/Log.hpp"
#include "../shared/Session.hpp"
#include "hyprland-input-capture-v1.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <sdbus-c++/Types.h>
#include <sdbus-c++/VTableItems.h>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unordered_map>
#include <wayland-util.h>

CInputCapturePortal::CInputCapturePortal(SP<CCHyprlandInputCaptureManagerV1> mgr) : m_sState(mgr) {
    Debug::log(LOG, "[input-capture] initializing input capture portal");

    mgr->setForceRelease([this](CCHyprlandInputCaptureManagerV1* r) { onForceRelease(); });

    mgr->setMotion([this](CCHyprlandInputCaptureManagerV1* r, wl_fixed_t x, wl_fixed_t y, wl_fixed_t dx, wl_fixed_t dy) {
        onMotion(wl_fixed_to_double(x), wl_fixed_to_double(y), wl_fixed_to_double(dx), wl_fixed_to_double(dy));
    });

    mgr->setKeymap([this](CCHyprlandInputCaptureManagerV1* r, hyprlandInputCaptureManagerV1KeymapFormat format, int32_t fd, uint32_t size) {
        onKeymap(format == HYPRLAND_INPUT_CAPTURE_MANAGER_V1_KEYMAP_FORMAT_XKB_V1 ? fd : 0, size);
    });

    mgr->setKey([this](CCHyprlandInputCaptureManagerV1* r, uint32_t key, hyprlandInputCaptureManagerV1KeyState state) { onKey(key, state); });

    mgr->setButton([this](CCHyprlandInputCaptureManagerV1* r, uint32_t button, hyprlandInputCaptureManagerV1ButtonState state) { onButton(button, state); });

    mgr->setAxis([this](CCHyprlandInputCaptureManagerV1* r, hyprlandInputCaptureManagerV1Axis axis, double value) { onAxis(axis, value); });

    mgr->setAxisValue120([this](CCHyprlandInputCaptureManagerV1* r, hyprlandInputCaptureManagerV1Axis axis, int32_t value120) { onAxis(axis, value120); });

    mgr->setAxisStop([this](CCHyprlandInputCaptureManagerV1* r, hyprlandInputCaptureManagerV1Axis axis) { onAxisStop(axis); });

    mgr->setFrame([this](CCHyprlandInputCaptureManagerV1* r) { onFrame(); });

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

    const std::shared_ptr<SSession> session = std::make_shared<SSession>();

    session->appid         = appID;
    session->requestHandle = requestHandle;
    session->sessionHandle = sessionHandle;
    session->sessionId     = sessionId;
    session->capabilities  = capabilities;

    session->session            = createDBusSession(sessionHandle);
    session->session->onDestroy = [session, this]() {
        disable(session->sessionHandle);
        session->status = STOPPED;
        session->eis->stopServer();
        session->eis.reset();

        Debug::log(LOG, "[input-capture] Session {} destroyed", session->sessionHandle.c_str());

        session->session.release();
    };

    session->request            = createDBusRequest(requestHandle);
    session->request->onDestroy = [session]() { session->request.release(); };

    session->eis = std::make_unique<EmulatedInputServer>("eis-" + sessionId, keymap);

    sessions.emplace(sessionHandle, session);

    std::unordered_map<std::string, sdbus::Variant> results;
    results["capabilities"] = sdbus::Variant{(uint)3};
    results["session_id"]   = sdbus::Variant{sessionId};
    return {0, results};
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
        zones.push_back(sdbus::Struct(o->width, o->height, o->x, o->y));
    }

    std::unordered_map<std::string, sdbus::Variant> results;
    results["zones"]    = sdbus::Variant{zones};
    results["zone_set"] = sdbus::Variant{++lastZoneSet};
    return {0, results};
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
        Debug::log(WARN, "[input-capture] Invalid zone set discarding barriers");
        return {1, {}}; //TODO: We should return failed_barries
    }

    sessions[sessionHandle]->barriers.clear();
    for (const auto& b : barriers) {
        uint                              id = b.at("barrier_id").get<uint>();
        int                               x1, y1, x2, y2;

        sdbus::Struct<int, int, int, int> p = b.at("position").get<sdbus::Struct<int, int, int, int>>();
        x1                                  = p.get<0>();
        y1                                  = p.get<1>();
        x2                                  = p.get<2>();
        y2                                  = p.get<3>();

        Debug::log(LOG, "[input-capture]  | barrier: {}, [{}, {}] [{}, {}]", id, x1, y1, x2, y2);
        sessions[sessionHandle]->barriers[id] = {id, x1, y1, x2, y2};
    }

    std::vector<uint>                               failedBarriers;

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

    disable(sessionHandle);
    return {0, {}};
}

dbUasv CInputCapturePortal::onEnable(sdbus::ObjectPath sessionHandle, std::string appID, std::unordered_map<std::string, sdbus::Variant> opts) {
    Debug::log(LOG, "[input-capture] New Enable request:");
    Debug::log(LOG, "[input-capture]  | {}", sessionHandle.c_str());
    Debug::log(LOG, "[input-capture]  | appid: {}", appID);

    if (!sessions.contains(sessionHandle)) {
        Debug::log(WARN, "[input-capture] Unknown session handle: {}", sessionHandle.c_str());
        return {1, {}};
    }

    sessions[sessionHandle]->status = ENABLED;
    return {0, {}};
}

dbUasv CInputCapturePortal::onRelease(sdbus::ObjectPath sessionHandle, std::string appID, std::unordered_map<std::string, sdbus::Variant> opts) {
    Debug::log(LOG, "[input-capture] New Release request:");
    Debug::log(LOG, "[input-capture]  | {}", sessionHandle.c_str());
    Debug::log(LOG, "[input-capture]  | appid: {}", appID);

    if (!sessionValid(sessionHandle))
        return {1, {}};

    uint32_t activationId = opts["activation_id"].get<uint32_t>();
    if (activationId != sessions[sessionHandle]->activationId) {
        Debug::log(WARN, "[input-capture] Invalid activation id {} expected {}", activationId, sessions[sessionHandle]->activationId);
        return {1, {}};
    }

    deactivate(sessionHandle);

    //TODO: maybe warp pointer

    return {0, {}};
}

sdbus::UnixFd CInputCapturePortal::onConnectToEIS(sdbus::ObjectPath sessionHandle, std::string appID, std::unordered_map<std::string, sdbus::Variant> opts) {
    Debug::log(LOG, "[input-capture] New ConnectToEIS request:");
    Debug::log(LOG, "[input-capture]  | {}", sessionHandle.c_str());
    Debug::log(LOG, "[input-capture]  | appid: {}", appID);

    if (!sessionValid(sessionHandle))
        return (sdbus::UnixFd)0;

    int sockfd = sessions[sessionHandle]->eis->getFileDescriptor();

    Debug::log(LOG, "[input-capture] Connected to the socket. File descriptor: {}", sockfd);
    return (sdbus::UnixFd)sockfd;
}

bool CInputCapturePortal::sessionValid(sdbus::ObjectPath sessionHandle) {
    if (!sessions.contains(sessionHandle)) {
        Debug::log(WARN, "[input-capture] Unknown session handle: {}", sessionHandle.c_str());
        return false;
    }

    return sessions[sessionHandle]->status != STOPPED;
}

void CInputCapturePortal::onForceRelease() {
    Debug::log(LOG, "[input-capture] Released every captures");
    for (auto [key, value] : sessions)
        disable(value->sessionHandle);
}

bool get_line_intersection(double p0_x, double p0_y, double p1_x, double p1_y, double p2_x, double p2_y, double p3_x, double p3_y, double* i_x, double* i_y) {
    float s1_x, s1_y, s2_x, s2_y;
    s1_x = p1_x - p0_x;
    s1_y = p1_y - p0_y;
    s2_x = p3_x - p2_x;
    s2_y = p3_y - p2_y;

    float s, t;
    s = (-s1_y * (p0_x - p2_x) + s1_x * (p0_y - p2_y)) / (-s2_x * s1_y + s1_x * s2_y);
    t = (s2_x * (p0_y - p2_y) - s2_y * (p0_x - p2_x)) / (-s2_x * s1_y + s1_x * s2_y);

    if (s >= 0 && s <= 1 && t >= 0 && t <= 1) {
        // Collision detected
        if (i_x != NULL)
            *i_x = p0_x + (t * s1_x);
        if (i_y != NULL)
            *i_y = p0_y + (t * s1_y);
        return 1;
    }

    return 0; // No collision
}

bool testCollision(SBarrier barrier, double px, double py, double nx, double ny) {
    return get_line_intersection(barrier.x1, barrier.y1, barrier.x2, barrier.y2, px, py, nx, ny, nullptr, nullptr);
}

uint32_t CInputCapturePortal::SSession::isColliding(double px, double py, double nx, double ny) {
    for (const auto& [key, value] : barriers)
        if (testCollision(value, px, py, nx, ny))
            return key;

    return 0;
}

void CInputCapturePortal::onMotion(double x, double y, double dx, double dy) {
    for (const auto& [key, session] : sessions) {
        int matched = session->isColliding(x, y, x - dx, y - dy);
        if (matched != 0)
            activate(session->sessionHandle, x, y, matched);

        session->motion(dx, dy);
    }
}

void CInputCapturePortal::onKey(uint32_t id, bool pressed) {
    for (const auto& [key, value] : sessions)
        value->key(id, pressed);
}

void CInputCapturePortal::onKeymap(int32_t fd, uint32_t size) {
    keymap.fd   = fd;
    keymap.size = size;
    for (const auto& [key, value] : sessions)
        value->keymap(keymap);
}

void CInputCapturePortal::onButton(uint32_t button, bool pressed) {
    for (const auto& [key, session] : sessions)
        session->button(button, pressed);
}

void CInputCapturePortal::onAxis(bool axis, double value) {
    for (const auto& [key, session] : sessions)
        session->axis(axis, value);
}

void CInputCapturePortal::onAxisValue120(bool axis, int32_t value120) {
    for (const auto& [key, session] : sessions)
        session->axisValue120(axis, value120);
}

void CInputCapturePortal::onAxisStop(bool axis) {
    for (const auto& [_, session] : sessions)
        session->axisStop(axis);
}

void CInputCapturePortal::onFrame() {
    for (const auto& [_, session] : sessions)
        session->frame();
}

void CInputCapturePortal::activate(sdbus::ObjectPath sessionHandle, double x, double y, uint32_t borderId) {
    if (!sessionValid(sessionHandle))
        return;

    auto session = sessions[sessionHandle];
    if (!session->activate(x, y, borderId))
        return;

    m_sState.manager->sendCapture();

    std::unordered_map<std::string, sdbus::Variant> results;
    results["activation_id"]   = sdbus::Variant{session->activationId};
    results["cursor_position"] = sdbus::Variant{sdbus::Struct<double, double>(x, y)};
    results["barrier_id"]      = sdbus::Variant{borderId};

    m_pObject->emitSignal("Activated").onInterface(INTERFACE_NAME).withArguments(sessionHandle, results);
}

bool CInputCapturePortal::SSession::activate(double x, double y, uint32_t borderId) {
    if (status != ENABLED)
        return false;

    activationId += 5;
    status = ACTIVATED;
    Debug::log(LOG, "[input-capture] Input captured for {} activationId: {}", sessionHandle.c_str(), activationId);
    eis->startEmulating(activationId);

    return true;
}

void CInputCapturePortal::deactivate(sdbus::ObjectPath sessionHandle) {
    if (!sessionValid(sessionHandle))
        return;

    auto session = sessions[sessionHandle];
    if (!session->deactivate())
        return;

    m_sState.manager->sendRelease();

    std::unordered_map<std::string, sdbus::Variant> options;
    options["activation_id"] = sdbus::Variant{session->activationId};

    m_pObject->emitSignal("Deactivated").onInterface(INTERFACE_NAME).withArguments(sessionHandle, options);
}

bool CInputCapturePortal::SSession::deactivate() {
    if (status != ACTIVATED)
        return false;

    Debug::log(LOG, "[input-capture] Input released for {}", sessionHandle.c_str());
    eis->stopEmulating();

    status = ENABLED;

    return true;
}

void CInputCapturePortal::zonesChanged() {
    if (sessions.empty())
        return;

    Debug::log(LOG, "[input-capture] Monitor layout has changed, notifing clients");

    for (auto& [key, value] : sessions) {
        if (!value->zoneChanged())
            continue;

        std::unordered_map<std::string, sdbus::Variant> options;
        m_pObject->emitSignal("ZonesChanged").onInterface(INTERFACE_NAME).withArguments(key, options);
    }
}

bool CInputCapturePortal::SSession::zoneChanged() {
    //TODO: notify EIS
    return true;
}

void CInputCapturePortal::disable(sdbus::ObjectPath sessionHandle) {
    if (!sessionValid(sessionHandle))
        return;

    auto session = sessions[sessionHandle];

    if (session->status == ACTIVATED)
        deactivate(sessionHandle);

    if (!session->disable())
        return;

    std::unordered_map<std::string, sdbus::Variant> options;
    m_pObject->emitSignal("Disabled").onInterface(INTERFACE_NAME).withArguments(sessionHandle, options);
}

bool CInputCapturePortal::SSession::disable() {
    status = CREATED;
    barriers.clear();
    Debug::log(LOG, "[input-capture] Session {} disabled", sessionHandle.c_str());
    return true;
}

void CInputCapturePortal::SSession::motion(double dx, double dy) {
    if (status != ACTIVATED)
        return;

    eis->sendMotion(dx, dy);
}

void CInputCapturePortal::SSession::keymap(Keymap keymap) {
    if (status == STOPPED)
        return;

    eis->setKeymap(keymap);
}

void CInputCapturePortal::SSession::key(uint32_t key, bool pressed) {
    if (status != ACTIVATED)
        return;

    eis->sendKey(key, pressed);
}

void CInputCapturePortal::SSession::button(uint32_t button, bool pressed) {
    if (status != ACTIVATED)
        return;

    eis->sendButton(button, pressed);
}

void CInputCapturePortal::SSession::axis(bool axis, double value) {
    if (status != ACTIVATED)
        return;

    double x = 0;
    double y = 0;

    if (axis)
        x = value;
    else
        y = value;

    eis->sendScrollDelta(x, y);
}

void CInputCapturePortal::SSession::axisValue120(bool axis, int32_t value) {
    if (status != ACTIVATED)
        return;

    int32_t x = 0;
    int32_t y = 0;

    if (axis)
        x = value;
    else
        y = value;

    eis->sendScrollDiscrete(x, y);
}

void CInputCapturePortal::SSession::axisStop(bool axis) {
    if (status != ACTIVATED)
        return;

    eis->sendScrollStop(axis, !axis);
}

void CInputCapturePortal::SSession::frame() {
    if (status != ACTIVATED)
        return;

    eis->sendPointerFrame();
}

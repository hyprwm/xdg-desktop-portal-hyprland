#include "InputCapture.hpp"

#include "../core/PortalManager.hpp"
#include "../helpers/Log.hpp"
#include "../shared/Session.hpp"
#include "hyprland-input-capture-v1.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unordered_map>
#include <wayland-util.h>

CInputCapturePortal::CInputCapturePortal(SP<CCHyprlandInputCaptureManagerV1> mgr) : m_sState(mgr) {
    Debug::log(LOG, "[input-capture] initializing input capture portal");

    mgr->setMotion([this](CCHyprlandInputCaptureManagerV1* r, wl_fixed_t x, wl_fixed_t y, wl_fixed_t dx, wl_fixed_t dy) {
        onMotion(wl_fixed_to_double(x), wl_fixed_to_double(y), wl_fixed_to_double(dx), wl_fixed_to_double(dy));
    });

    mgr->setKey([this](CCHyprlandInputCaptureManagerV1* r, uint32_t key, hyprlandInputCaptureManagerV1KeyState state) { onKey(key, state); });

    mgr->setButton([this](CCHyprlandInputCaptureManagerV1* r, uint32_t button, hyprlandInputCaptureManagerV1ButtonState state) { onButton(button, state); });

    mgr->setAxis([this](CCHyprlandInputCaptureManagerV1* r, hyprlandInputCaptureManagerV1Axis axis, double value) { onAxis(axis, value); });

    mgr->setAxisValue120([this](CCHyprlandInputCaptureManagerV1* r, hyprlandInputCaptureManagerV1Axis axis, int32_t value120) { onAxis(axis, value120); });

    mgr->setAxisStop([this](CCHyprlandInputCaptureManagerV1* r, hyprlandInputCaptureManagerV1Axis axis) { onAxisStop(axis); });

    mgr->setFrame([this](CCHyprlandInputCaptureManagerV1* r) { onFrame(); });

    m_pObject = sdbus::createObject(*g_pPortalManager->getConnection(), OBJECT_PATH);

    m_pObject->registerMethod(INTERFACE_NAME, "CreateSession", "oossa{sv}", "ua{sv}", [&](sdbus::MethodCall c) { onCreateSession(c); });
    m_pObject->registerMethod(INTERFACE_NAME, "GetZones", "oosa{sv}", "ua{sv}", [&](sdbus::MethodCall c) { onGetZones(c); });
    m_pObject->registerMethod(INTERFACE_NAME, "SetPointerBarriers", "oosa{sv}aa{sv}u", "ua{sv}", [&](sdbus::MethodCall c) { onSetPointerBarriers(c); });
    m_pObject->registerMethod(INTERFACE_NAME, "Enable", "osa{sv}", "ua{sv}", [&](sdbus::MethodCall c) { onEnable(c); });
    m_pObject->registerMethod(INTERFACE_NAME, "Disable", "osa{sv}", "ua{sv}", [&](sdbus::MethodCall c) { onDisable(c); });
    m_pObject->registerMethod(INTERFACE_NAME, "Release", "osa{sv}", "ua{sv}", [&](sdbus::MethodCall c) { onRelease(c); });
    m_pObject->registerMethod(INTERFACE_NAME, "ConnectToEIS", "osa{sv}", "h", [&](sdbus::MethodCall c) { onConnectToEIS(c); });

    m_pObject->registerProperty(INTERFACE_NAME, "SupportedCapabilities", "u", [](sdbus::PropertyGetReply& reply) { reply << (uint32_t)(1 | 2); });
    m_pObject->registerProperty(INTERFACE_NAME, "version", "u", [](sdbus::PropertyGetReply& reply) { reply << (uint32_t)1; });

    m_pObject->finishRegistration();

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

void CInputCapturePortal::onCreateSession(sdbus::MethodCall& call) {
    Debug::log(LOG, "[input-capture] New session:");

    sdbus::ObjectPath requestHandle, sessionHandle;

    call >> requestHandle;
    call >> sessionHandle;

    std::string appID;
    call >> appID;

    std::string parentWindow;
    call >> parentWindow;

    std::unordered_map<std::string, sdbus::Variant> options;
    call >> options;
    uint32_t capabilities = options["capabilities"];

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

        Debug::log(LOG, "[input-capture] Session {} destroyed", session->sessionHandle.c_str());

        session->session.release();
    };

    session->request            = createDBusRequest(requestHandle);
    session->request->onDestroy = [session]() { session->request.release(); };

    session->eis = std::make_unique<EmulatedInputServer>("eis-" + sessionId);

    sessions.emplace(sessionHandle, session);

    std::unordered_map<std::string, sdbus::Variant> results;
    results["capabilities"] = (uint)3;
    results["session_id"]   = sessionId;

    auto reply = call.createReply();
    reply << (uint32_t)0;
    reply << results;
    reply.send();
}

void CInputCapturePortal::onGetZones(sdbus::MethodCall& call) {
    Debug::log(LOG, "[input-capture] New GetZones request:");

    sdbus::ObjectPath requestHandle, sessionHandle;

    call >> requestHandle;
    call >> sessionHandle;

    std::string appID;
    call >> appID;

    Debug::log(LOG, "[input-capture]  | {}", requestHandle.c_str());
    Debug::log(LOG, "[input-capture]  | {}", sessionHandle.c_str());
    Debug::log(LOG, "[input-capture]  | appid: {}", appID);

    if (!sessionValid(sessionHandle))
        return;

    std::vector<sdbus::Struct<uint32_t, uint32_t, int32_t, int32_t>> zones;
    for (auto& o : g_pPortalManager->getAllOutputs()) {
        zones.push_back(sdbus::Struct(o->width, o->height, o->x, o->y));
    }

    std::unordered_map<std::string, sdbus::Variant> results;
    results["zones"]    = zones;
    results["zone_set"] = ++lastZoneSet;

    auto reply = call.createReply();
    reply << (uint32_t)0;
    reply << results;
    reply.send();
}

void CInputCapturePortal::onSetPointerBarriers(sdbus::MethodCall& call) {
    Debug::log(LOG, "[input-capture] New SetPointerBarriers request:");

    sdbus::ObjectPath requestHandle, sessionHandle;

    call >> requestHandle;
    call >> sessionHandle;

    std::string appID;
    call >> appID;

    Debug::log(LOG, "[input-capture]  | {}", requestHandle.c_str());
    Debug::log(LOG, "[input-capture]  | {}", sessionHandle.c_str());
    Debug::log(LOG, "[input-capture]  | appid: {}", appID);

    if (!sessionValid(sessionHandle))
        return complete(call);

    std::unordered_map<std::string, sdbus::Variant> options;
    call >> options;

    std::vector<std::unordered_map<std::string, sdbus::Variant>> barriers;
    call >> barriers;

    uint32_t zoneSet;
    call >> zoneSet;
    Debug::log(LOG, "[input-capture]  | zoneSet: {}", zoneSet);

    if (zoneSet != lastZoneSet) {
        Debug::log(WARN, "[input-capture] Invalid zone set discarding barriers");
        complete(call); //TODO: We should return failed_barries
        return;
    }

    sessions[sessionHandle]->barriers.clear();
    for (const auto& b : barriers) {
        uint                              id = b.at("barrier_id");
        int                               x1, y1, x2, y2;

        sdbus::Struct<int, int, int, int> p = b.at("position");
        x1                                  = p.get<0>();
        y1                                  = p.get<1>();
        x2                                  = p.get<2>();
        y2                                  = p.get<3>();

        Debug::log(LOG, "[input-capture]  | barrier: {}, [{}, {}] [{}, {}]", id, x1, y1, x2, y2);
        sessions[sessionHandle]->barriers[id] = {id, x1, y1, x2, y2};
    }

    std::vector<uint>                               failedBarriers;

    std::unordered_map<std::string, sdbus::Variant> results;
    results["failed_barriers"] = failedBarriers;

    auto reply = call.createReply();
    reply << (uint32_t)0;
    reply << results;
    reply.send();
}

void CInputCapturePortal::onDisable(sdbus::MethodCall& call) {
    Debug::log(LOG, "[input-capture] New Disable request:");

    sdbus::ObjectPath sessionHandle;
    call >> sessionHandle;

    std::string appID;
    call >> appID;

    Debug::log(LOG, "[input-capture]  | {}", sessionHandle.c_str());
    Debug::log(LOG, "[input-capture]  | appid: {}", appID);

    if (!sessionValid(sessionHandle))
        return complete(call);

    disable(sessionHandle);

    complete(call);
}

void CInputCapturePortal::onEnable(sdbus::MethodCall& call) {
    Debug::log(LOG, "[input-capture] New Enable request:");

    sdbus::ObjectPath sessionHandle;
    call >> sessionHandle;

    std::string appID;
    call >> appID;

    Debug::log(LOG, "[input-capture]  | {}", sessionHandle.c_str());
    Debug::log(LOG, "[input-capture]  | appid: {}", appID);

    if (!sessionValid(sessionHandle))
        return complete(call);

    sessions[sessionHandle]->status = ENABLED;

    complete(call);
}

void CInputCapturePortal::onRelease(sdbus::MethodCall& call) {
    Debug::log(LOG, "[input-capture] New Release request:");

    sdbus::ObjectPath sessionHandle;
    call >> sessionHandle;

    std::string appID;
    call >> appID;

    Debug::log(LOG, "[input-capture]  | {}", sessionHandle.c_str());
    Debug::log(LOG, "[input-capture]  | appid: {}", appID);

    if (!sessionValid(sessionHandle))
        return complete(call);

    std::unordered_map<std::string, sdbus::Variant> options;
    call >> options;
    uint32_t activationId = options["activation_id"];
    if (activationId != sessions[sessionHandle]->activationId) {
        Debug::log(WARN, "[input-capture] Invalid activation id {} expected {}", activationId, sessions[sessionHandle]->activationId);
        complete(call);
        return;
    }

    deactivate(sessionHandle);

    //TODO: maybe warp pointer

    complete(call);
}

void CInputCapturePortal::onConnectToEIS(sdbus::MethodCall& call) {
    Debug::log(LOG, "[input-capture] New ConnectToEIS request:");

    sdbus::ObjectPath sessionHandle;
    call >> sessionHandle;

    std::string appID;
    call >> appID;

    std::unordered_map<std::string, sdbus::Variant> options;
    call >> options;

    Debug::log(LOG, "[input-capture]  | {}", sessionHandle.c_str());
    Debug::log(LOG, "[input-capture]  | appid: {}", appID);

    if (!sessionValid(sessionHandle))
        return complete(call);

    int sockfd = sessions[sessionHandle]->eis->getFileDescriptor();

    Debug::log(LOG, "[input-capture] Connected to the socket. File descriptor: {}", sockfd);
    auto reply = call.createReply();
    reply << (sdbus::UnixFd)sockfd;
    reply.send();
}

bool CInputCapturePortal::sessionValid(sdbus::ObjectPath sessionHandle) {
    if (!sessions.contains(sessionHandle)) {
        Debug::log(WARN, "[input-capture] Unknown session handle: {}", sessionHandle.c_str());
        return false;
    }

    return sessions[sessionHandle]->status != STOPPED;
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
            activate(key, x, y, matched);

        session->motion(dx, dy);
    }
}

void CInputCapturePortal::onKey(uint32_t id, bool pressed) {
    for (const auto& [key, value] : sessions)
        value->key(id, pressed);
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

    auto signal = m_pObject->createSignal(INTERFACE_NAME, "Activated");
    signal << sessionHandle;

    g_pPortalManager->m_sPortals.inputCapture->INTERFACE_NAME;

    std::unordered_map<std::string, sdbus::Variant> results;
    results["activation_id"]   = session->activationId;
    results["cursor_position"] = sdbus::Struct<double, double>(x, y);
    results["barrier_id"]      = borderId;
    signal << results;

    m_pObject->emitSignal(signal);
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

    auto signal = m_pObject->createSignal(INTERFACE_NAME, "Deactivated");
    signal << sessionHandle;
    std::unordered_map<std::string, sdbus::Variant> options;
    options["activation_id"] = session->activationId;
    signal << options;

    m_pObject->emitSignal(signal);
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

        auto signal = m_pObject->createSignal(INTERFACE_NAME, "Deactivated");
        signal << key;

        std::unordered_map<std::string, sdbus::Variant> options;
        signal << options;

        m_pObject->emitSignal(signal);
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

    session->eis->stopServer();
    session->eis.reset();

    auto signal = m_pObject->createSignal(INTERFACE_NAME, "Disable");
    signal << sessionHandle;

    std::unordered_map<std::string, sdbus::Variant> options;
    signal << options;

    m_pObject->emitSignal(signal);
}

bool CInputCapturePortal::SSession::disable() {
    status = STOPPED;

    Debug::log(LOG, "[input-capture] Session {} disabled", sessionHandle.c_str());
    return true;
}

void CInputCapturePortal::SSession::motion(double dx, double dy) {
    if (status != ACTIVATED)
        return;

    eis->sendMotion(dx, dy);
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

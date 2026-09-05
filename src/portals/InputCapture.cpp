#include "InputCapture.hpp"

#include "../core/PortalManager.hpp"
#include "../helpers/Log.hpp"
#include "../shared/Session.hpp"
#include "hyprland-input-capture-v1.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <hyprutils/memory/UniquePtr.hpp>
#include <memory>
#include <sdbus-c++/Error.h>
#include <set>
#include <sdbus-c++/Types.h>
#include <sdbus-c++/VTableItems.h>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unordered_map>
#include <vector>
#include <wayland-client-core.h>
#include <wayland-util.h>

static int32_t logicalSize(uint32_t size, double scale) {
    if (scale <= 0.0)
        return static_cast<int32_t>(size);
    return std::max(1, static_cast<int32_t>(std::lround(static_cast<double>(size) / scale)));
}

struct SLogicalOutputGeometry {
    std::string name;
    int32_t     x             = 0;
    int32_t     y             = 0;
    int32_t     width         = 0;
    int32_t     height        = 0;
    double      scale         = 1.0;
    bool        fromXDGOutput = false;
};

static SLogicalOutputGeometry getLogicalGeometry(const std::unique_ptr<SOutput>& output) {
    if (output->logicalPositionValid && output->logicalSizeValid) {
        return {
            .name          = output->name,
            .x             = output->logicalX,
            .y             = output->logicalY,
            .width         = output->logicalWidth,
            .height        = output->logicalHeight,
            .scale         = output->scale,
            .fromXDGOutput = true,
        };
    }

    return {
        .name          = output->name,
        .x             = output->x,
        .y             = output->y,
        .width         = logicalSize(output->width, output->scale),
        .height        = logicalSize(output->height, output->scale),
        .scale         = output->scale,
        .fromXDGOutput = false,
    };
}

static bool rangeContains(int start, int end, int value) {
    return start <= value && value <= end;
}

static bool intervalOverlapsInclusive(int a1, int a2, int b1, int b2) {
    return std::max(a1, b1) <= std::min(a2, b2);
}

static bool isVerticalBarrierOnExteriorBoundary(int x, int y1, int y2) {
    std::set<int> breakpoints = {y1, y2 + 1};

    for (const auto& output : g_pPortalManager->getAllOutputs()) {
        const auto logical = getLogicalGeometry(output);
        if (logical.width <= 0 || logical.height <= 0)
            return false;

        const int top    = logical.y;
        const int bottom = logical.y + logical.height - 1;
        if (!intervalOverlapsInclusive(y1, y2, top, bottom))
            continue;

        if (x == logical.x || x == logical.x + logical.width) {
            breakpoints.insert(std::max(y1, top));
            breakpoints.insert(std::min(y2, bottom) + 1);
        }
    }

    const std::vector<int> points{breakpoints.begin(), breakpoints.end()};
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        const int segmentStart = points[i];
        const int segmentEnd   = points[i + 1] - 1;
        if (segmentStart > segmentEnd)
            continue;

        bool hasLeftMonitor  = false;
        bool hasRightMonitor = false;
        for (const auto& output : g_pPortalManager->getAllOutputs()) {
            const auto logical = getLogicalGeometry(output);
            if (logical.width <= 0 || logical.height <= 0)
                return false;

            const int top    = logical.y;
            const int bottom = logical.y + logical.height - 1;
            if (!intervalOverlapsInclusive(segmentStart, segmentEnd, top, bottom))
                continue;

            if (x == logical.x + logical.width && rangeContains(top, bottom, segmentStart))
                hasLeftMonitor = true;

            if (x == logical.x && rangeContains(top, bottom, segmentStart))
                hasRightMonitor = true;
        }

        if (hasLeftMonitor == hasRightMonitor)
            return false;
    }

    return true;
}

static bool isHorizontalBarrierOnExteriorBoundary(int y, int x1, int x2) {
    std::set<int> breakpoints = {x1, x2 + 1};

    for (const auto& output : g_pPortalManager->getAllOutputs()) {
        const auto logical = getLogicalGeometry(output);
        if (logical.width <= 0 || logical.height <= 0)
            return false;

        const int left  = logical.x;
        const int right = logical.x + logical.width - 1;
        if (!intervalOverlapsInclusive(x1, x2, left, right))
            continue;

        if (y == logical.y || y == logical.y + logical.height) {
            breakpoints.insert(std::max(x1, left));
            breakpoints.insert(std::min(x2, right) + 1);
        }
    }

    const std::vector<int> points{breakpoints.begin(), breakpoints.end()};
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        const int segmentStart = points[i];
        const int segmentEnd   = points[i + 1] - 1;
        if (segmentStart > segmentEnd)
            continue;

        bool hasTopMonitor    = false;
        bool hasBottomMonitor = false;
        for (const auto& output : g_pPortalManager->getAllOutputs()) {
            const auto logical = getLogicalGeometry(output);
            if (logical.width <= 0 || logical.height <= 0)
                return false;

            const int left  = logical.x;
            const int right = logical.x + logical.width - 1;
            if (!intervalOverlapsInclusive(segmentStart, segmentEnd, left, right))
                continue;

            if (y == logical.y + logical.height && rangeContains(left, right, segmentStart))
                hasTopMonitor = true;

            if (y == logical.y && rangeContains(left, right, segmentStart))
                hasBottomMonitor = true;
        }

        if (hasTopMonitor == hasBottomMonitor)
            return false;
    }

    return true;
}

static bool isBarrierValid(int x1, int y1, int x2, int y2) {
    if (x1 != x2 && y1 != y2) //At least one axis should be aligned
        return false;

    if (x1 == x2 && y1 == y2) //The barrier should have non-null area
        return false;

    if (x1 > x2)
        std::swap(x1, x2);

    if (y1 > y2)
        std::swap(y1, y2);

    if (x1 == x2)
        return isVerticalBarrierOnExteriorBoundary(x1, y1, y2);

    return isHorizontalBarrierOnExteriorBoundary(y1, x1, x2);
}

CInputCapturePortal::CInputCapturePortal(SP<CCHyprlandInputCaptureManagerV1> mgr) : m_sState{mgr} {
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

    uint32_t capabilities = 0;
    if (options.contains("capabilities")) {
        try {
            capabilities = options["capabilities"].get<uint32_t>();
        } catch (std::exception& e) {
            Debug::log(WARN, "[input-capture] Invalid capabilities option: {}", e.what());
            return {1, {}};
        }
    }

    Debug::log(LOG, "[input-capture]  | {}", requestHandle.c_str());
    Debug::log(LOG, "[input-capture]  | {}", sessionHandle.c_str());
    Debug::log(LOG, "[input-capture]  | appid: {}", appID);
    Debug::log(LOG, "[input-capture]  | parent_window: {}", parentWindow);
    Debug::log(LOG, "[input-capture]  | capabilities : {}", capabilities);

    std::string sessionId = "input-capture-" + std::to_string(m_sessionCounter++);
    Debug::log(LOG, "[input-capture]  | sessionId : {}", sessionId);

    const std::shared_ptr<SSession> session =
        std::make_shared<SSession>(requestHandle, sessionHandle, sessionId, capabilities, m_sState.manager->sendCreateSession(sessionId.c_str()));

    m_sessions.emplace(sessionHandle, session);

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
    uint32_t                                                         zoneId = 0;
    for (auto& o : g_pPortalManager->getAllOutputs()) {
        const auto logical = getLogicalGeometry(o);
        zones.push_back(sdbus::Struct(static_cast<uint32_t>(logical.width), static_cast<uint32_t>(logical.height), logical.x, logical.y));
        Debug::log(LOG, "[input-capture]  | zone {} output {} pos {}x{} size {}x{} scale {} source {}", zoneId++, logical.name, logical.x, logical.y, logical.width, logical.height,
                   logical.scale, logical.fromXDGOutput ? "xdg-output" : "wl_output-fallback");
    }

    std::unordered_map<std::string, sdbus::Variant> results;
    results["zones"]    = sdbus::Variant{zones};
    results["zone_set"] = sdbus::Variant{m_lastZoneSet};
    Debug::log(LOG, "[input-capture]  | zoneSet: {}", m_lastZoneSet);
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

    std::vector<uint32_t> failedBarriers;
    if (zoneSet != m_lastZoneSet) {
        for (const auto& b : barriers) {
            if (!b.contains("barrier_id"))
                continue;

            try {
                failedBarriers.push_back(b.at("barrier_id").get<uint32_t>());
            } catch (std::exception& e) { Debug::log(WARN, "[input-capture] invalid barrier_id in stale zone set request: {}", e.what()); }
        }

        Debug::log(WARN, "[input-capture] Invalid zone set {}, current {}, discarding {} barriers", zoneSet, m_lastZoneSet, failedBarriers.size());

        std::unordered_map<std::string, sdbus::Variant> results;
        results["failed_barriers"] = sdbus::Variant{failedBarriers};
        return {0, results};
    }

    auto& session = m_sessions[sessionHandle];
    session->barrierIdMap.clear();
    session->whandle->sendClearBarriers();

    for (const auto& b : barriers) {
        if (!b.contains("barrier_id") || !b.contains("position")) {
            Debug::log(WARN, "[input-capture] barrier missing barrier_id or position");
            continue;
        }

        uint32_t id = 0;
        int      x1, y1, x2, y2;

        try {
            id = b.at("barrier_id").get<uint32_t>();

            sdbus::Struct<int, int, int, int> p = b.at("position").get<sdbus::Struct<int, int, int, int>>();
            x1                                  = p.get<0>();
            y1                                  = p.get<1>();
            x2                                  = p.get<2>();
            y2                                  = p.get<3>();
        } catch (std::exception& e) {
            Debug::log(WARN, "[input-capture] invalid barrier payload: {}", e.what());
            failedBarriers.push_back(id);
            continue;
        }

        Debug::log(LOG, "[input-capture]  | barrier request {} [{}, {}] [{}, {}] zoneSet {}", id, x1, y1, x2, y2, zoneSet);
        const bool valid = isBarrierValid(x1, y1, x2, y2);
        if (id == 0) {
            Debug::log(LOG, "[input-capture]  | barrier: 0 invalid (skipping)");
            failedBarriers.push_back(id);
            continue;
        }

        if (!valid) {
            Debug::log(LOG, "[input-capture]  | barrier: {} [{}, {}] [{}, {}] valid: false", id, x1, y1, x2, y2);
            failedBarriers.push_back(id);
            continue;
        }

        // Clients may reuse barrier_id values in one request. Use a unique internal
        // ID per barrier and map activation callbacks back to the client ID.
        uint32_t internalId = m_barrierIdCounter++;
        if (internalId == 0)
            internalId = m_barrierIdCounter++;

        session->barrierIdMap[internalId] = id;

        const uint32_t ux1 = static_cast<uint32_t>(x1);
        const uint32_t uy1 = static_cast<uint32_t>(y1);
        const uint32_t ux2 = static_cast<uint32_t>(x2);
        const uint32_t uy2 = static_cast<uint32_t>(y2);
        Debug::log(LOG, "[input-capture]  | forwarding barrier {} as logical coords [{}, {}] [{}, {}] internal {}", id, ux1, uy1, ux2, uy2, internalId);
        session->whandle->sendAddBarrier(zoneSet, internalId, ux1, uy1, ux2, uy2);
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

    m_sessions[sessionHandle]->whandle->sendDisable();
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

    m_sessions[sessionHandle]->whandle->sendEnable();
    return {0, {}};
}

dbUasv CInputCapturePortal::onRelease(sdbus::ObjectPath sessionHandle, std::string appID, std::unordered_map<std::string, sdbus::Variant> opts) {
    Debug::log(LOG, "[input-capture] New Release request:");
    Debug::log(LOG, "[input-capture]  | {}", sessionHandle.c_str());
    Debug::log(LOG, "[input-capture]  | appid: {}", appID);

    if (!sessionValid(sessionHandle))
        return {1, {}};

    if (!opts.contains("activation_id")) {
        Debug::log(WARN, "[input-capture] Release missing activation_id");
        return {1, {}};
    }

    uint32_t activationId = 0;
    try {
        activationId = opts["activation_id"].get<uint32_t>();
    } catch (std::exception& e) {
        Debug::log(WARN, "[input-capture] invalid activation_id: {}", e.what());
        return {1, {}};
    }
    Debug::log(LOG, "[input-capture]  | activationId: {}", activationId);

    double x = -1;
    double y = -1;
    if (opts.contains("cursor_position")) {
        try {
            auto [px, py] = opts["cursor_position"].get<sdbus::Struct<double, double>>();
            Debug::log(LOG, "[input-capture]  | cursorPosition: {} {}", px, py);
            x = px;
            y = py;
        } catch (std::exception& e) { Debug::log(WARN, "[input-capture] invalid cursor_position: {}", e.what()); }
    }
    m_sessions[sessionHandle]->whandle->sendRelease(activationId, x, y); //TODO: send pointer position for warping

    return {0, {}};
}

sdbus::UnixFd CInputCapturePortal::onConnectToEIS(sdbus::ObjectPath sessionHandle, std::string appID, std::unordered_map<std::string, sdbus::Variant> opts) {
    Debug::log(LOG, "[input-capture] New ConnectToEIS request:");
    Debug::log(LOG, "[input-capture]  | {}", sessionHandle.c_str());
    Debug::log(LOG, "[input-capture]  | appid: {}", appID);

    if (!sessionValid(sessionHandle))
        throw sdbus::Error{sdbus::Error::Name{"org.freedesktop.portal.Error.Failed"}, "No input-capture session found"};

    for (size_t i = 0; i < 3 && m_sessions[sessionHandle]->eisFD < 0; ++i) {
        if (wl_display_roundtrip(g_pPortalManager->m_sWaylandConnection.display) < 0)
            break;
    }

    if (m_sessions[sessionHandle]->eisFD < 0)
        throw sdbus::Error{sdbus::Error::Name{"org.freedesktop.portal.Error.Failed"}, "EIS fd is not ready"};

    return (sdbus::UnixFd)m_sessions[sessionHandle]->eisFD;
}

bool CInputCapturePortal::sessionValid(sdbus::ObjectPath sessionHandle) {
    if (!m_sessions.contains(sessionHandle)) {
        Debug::log(WARN, "[input-capture] Unknown session handle: {}", sessionHandle.c_str());
        return false;
    }

    return !m_sessions[sessionHandle]->dead;
}

void CInputCapturePortal::removeSession(sdbus::ObjectPath sessionHandle) {
    m_sessions.erase(sessionHandle);
}

void CInputCapturePortal::zonesChanged() {
    if (m_sessions.empty())
        return;

    Debug::log(LOG, "[input-capture] Monitor layout has changed, notifing clients");
    m_lastZoneSet++;

    for (auto& [key, value] : m_sessions) {
        if (!sessionValid(value->sessionHandle))
            continue;

        std::unordered_map<std::string, sdbus::Variant> options;
        options["zone_set"] = sdbus::Variant{m_lastZoneSet - 1};
        m_pObject->emitSignal("ZonesChanged").onInterface(INTERFACE_NAME).withArguments(value->sessionHandle, options);
    }
}

void CInputCapturePortal::activate(sdbus::ObjectPath sessionHandle, uint32_t activationId, double x, double y, uint32_t borderId) {
    Debug::log(LOG, "[input-capture] activate, activationId {}, barrierId {}, x {}, y {}", activationId, borderId, x, y);
    if (!sessionValid(sessionHandle))
        return;

    const auto&    session         = m_sessions[sessionHandle];
    const auto     mappedBarrierId = session->barrierIdMap.find(borderId);
    const uint32_t clientBarrierId = mappedBarrierId != session->barrierIdMap.end() ? mappedBarrierId->second : borderId;

    Debug::log(LOG, "[input-capture]  | activation mapping: internal barrier {} -> client barrier {}, mapped: {}, activationId {}, x {}, y {}", borderId, clientBarrierId,
               mappedBarrierId != session->barrierIdMap.end(), activationId, x, y);

    std::unordered_map<std::string, sdbus::Variant> results;
    results["activation_id"]   = sdbus::Variant{activationId};
    results["cursor_position"] = sdbus::Variant{sdbus::Struct<double, double>(x, y)};
    results["barrier_id"]      = sdbus::Variant{clientBarrierId};

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

#include "Screencopy.hpp"
#include "../core/PortalManager.hpp"
#include "../helpers/Log.hpp"
#include "../helpers/MiscFunctions.hpp"

#include <pipewire/pipewire.h>

void onCloseRequest(sdbus::MethodCall& call, CScreencopyPortal::SSession* sess) {
    if (!sess || !sess->request)
        return;

    auto r = call.createReply();
    r.send();

    sess->request.release();
}

void onCloseSession(sdbus::MethodCall& call, CScreencopyPortal::SSession* sess) {
    if (!sess || !sess->session)
        return;

    auto r = call.createReply();
    r.send();

    sess->session.release();
}

void CScreencopyPortal::onCreateSession(sdbus::MethodCall& call) {
    sdbus::ObjectPath requestHandle, sessionHandle;

    call >> requestHandle;
    call >> sessionHandle;

    std::string appID;
    call >> appID;

    Debug::log(LOG, "[screencopy] New session:");
    Debug::log(LOG, "[screencopy]  | {}", requestHandle.c_str());
    Debug::log(LOG, "[screencopy]  | {}", sessionHandle.c_str());
    Debug::log(LOG, "[screencopy]  | appid: {}", appID);

    const auto PSESSION = m_vSessions.emplace_back(std::make_unique<SSession>(appID, requestHandle, sessionHandle)).get();

    // create objects
    PSESSION->request = sdbus::createObject(*g_pPortalManager->getConnection(), requestHandle);
    PSESSION->session = sdbus::createObject(*g_pPortalManager->getConnection(), sessionHandle);

    PSESSION->request->registerMethod("org.freedesktop.impl.portal.Request", "Close", "", "", [&](sdbus::MethodCall c) { onCloseRequest(c, PSESSION); });
    PSESSION->session->registerMethod("org.freedesktop.impl.portal.Session", "Close", "", "", [&](sdbus::MethodCall c) { onCloseSession(c, PSESSION); });

    PSESSION->request->finishRegistration();
    PSESSION->session->finishRegistration();

    auto reply = call.createReply();
    reply << (uint32_t)0;
    reply << std::unordered_map<std::string, sdbus::Variant>{};
    reply.send();
}

void CScreencopyPortal::onSelectSources(sdbus::MethodCall& call) {
    sdbus::ObjectPath requestHandle, sessionHandle;

    call >> requestHandle;
    call >> sessionHandle;

    std::string appID;
    call >> appID;

    Debug::log(LOG, "[screencopy] SelectSources:");
    Debug::log(LOG, "[screencopy]  | {}", requestHandle.c_str());
    Debug::log(LOG, "[screencopy]  | {}", sessionHandle.c_str());
    Debug::log(LOG, "[screencopy]  | appid: {}", appID);

    const auto PSESSION = getSession(sessionHandle);

    if (!PSESSION) {
        Debug::log(ERR, "[screencopy] SelectSources: no session found??");
        auto reply = call.createErrorReply(sdbus::Error{"NOSESSION", "No session found"});
        reply << (uint32_t)1;
        reply.send();
        return;
    }

    std::unordered_map<std::string, sdbus::Variant> options;

    call >> options;

    struct {
        bool        exists = false;
        std::string token, output;
        uint32_t    windowHandle;
        bool        withCursor;
        uint64_t    timeIssued;
    } restoreData;

    for (auto& [key, val] : options) {

        if (key == "cursor_mode") {
            PSESSION->cursorMode = val.get<uint32_t>();
            Debug::log(LOG, "[screencopy] option cursor_mode to {}", PSESSION->cursorMode);
        } else if (key == "restore_data") {
            // suv
            // v -> r(susbt)
            std::string issuer;
            uint32_t    version;
            auto        suv = val.get<sdbus::Struct<std::string, uint32_t, sdbus::Variant>>();
            issuer          = suv.get<0>();
            version         = suv.get<1>();

            sdbus::Variant data = suv.get<2>();

            if (issuer != "hyprland") {
                Debug::log(LOG, "[screencopy] Restore token from {}, ignoring", issuer);
                continue;
            }

            Debug::log(LOG, "[screencopy] Restore token from {} ver {}", issuer, version);

            if (version != 2) {
                Debug::log(LOG, "[screencopy] Restore token ver unsupported, skipping", issuer);
                continue;
            }

            auto susbt = data.get<sdbus::Struct<std::string, uint32_t, std::string, bool, uint64_t>>();

            restoreData.exists = true;

            restoreData.token        = susbt.get<0>();
            restoreData.windowHandle = susbt.get<1>();
            restoreData.output       = susbt.get<2>();
            restoreData.withCursor   = susbt.get<3>();
            restoreData.timeIssued   = susbt.get<4>();

            Debug::log(LOG, "[screencopy] Restore token {} with data: {} {} {} {}", restoreData.token, restoreData.windowHandle, restoreData.output, restoreData.withCursor,
                       restoreData.timeIssued);
        } else if (key == "persist_mode") {
            PSESSION->persistMode = val.get<uint32_t>();
            Debug::log(LOG, "[screencopy] option persist_mode to {}", PSESSION->persistMode);
        } else {
            Debug::log(LOG, "[screencopy] unused option {}", key);
        }
    }

    const auto SHAREDATA = promptForScreencopySelection();

    Debug::log(LOG, "[screencopy] SHAREDATA returned selection {}", (int)SHAREDATA.type);

    auto reply = call.createReply();
    reply << (uint32_t)(SHAREDATA.type == TYPE_INVALID ? 1 : 0);
    reply << std::unordered_map<std::string, sdbus::Variant>{};
    reply.send();
}

void CScreencopyPortal::onStart(sdbus::MethodCall& call) {
    sdbus::ObjectPath requestHandle, sessionHandle;

    call >> requestHandle;
    call >> sessionHandle;

    std::string appID, parentWindow;
    call >> appID;
    call >> parentWindow;

    Debug::log(LOG, "[screencopy] Start:");
    Debug::log(LOG, "[screencopy]  | {}", requestHandle.c_str());
    Debug::log(LOG, "[screencopy]  | {}", sessionHandle.c_str());
    Debug::log(LOG, "[screencopy]  | appid: {}", appID);
    Debug::log(LOG, "[screencopy]  | parent_window: {}", parentWindow);

    const auto PSESSION = getSession(sessionHandle);

    if (!PSESSION) {
        Debug::log(ERR, "[screencopy] Start: no session found??");
        auto reply = call.createErrorReply(sdbus::Error{"NOSESSION", "No session found"});
        reply << (uint32_t)1;
        reply.send();
        return;
    }

    // TODO: start cast

    auto reply = call.createReply();
    reply << (uint32_t)0;

    std::unordered_map<std::string, sdbus::Variant> options;

    if (PSESSION->persistMode != 0) {
        // give them a token :)
        sdbus::Struct<std::string, uint32_t, std::string, bool, uint64_t> structData{"todo", (uint32_t)PSESSION->selection.windowHandle, PSESSION->selection.output,
                                                                                     (bool)PSESSION->cursorMode, (uint64_t)time(NULL)};
        sdbus::Variant                                                    restoreData{structData};
        sdbus::Struct<std::string, uint32_t, sdbus::Variant>              fullRestoreStruct{"hyprland", 2, restoreData};
        options["restore_data"] = sdbus::Variant{fullRestoreStruct};
    }

    uint32_t type = 0;
    switch (PSESSION->selection.type) {
        case TYPE_OUTPUT: type = 1 << MONITOR; break;
        case TYPE_WINDOW: type = 1 << WINDOW; break;
        case TYPE_GEOMETRY:
        case TYPE_WORKSPACE: type = 1 << VIRTUAL; break;
        default: type = 0; break;
    }
    options["source_type"] = type;

    reply << options;

    reply.send();
}

CScreencopyPortal::SSession* CScreencopyPortal::getSession(sdbus::ObjectPath& path) {
    for (auto& s : m_vSessions) {
        if (s->sessionHandle == path)
            return s.get();
    }

    return nullptr;
}

CScreencopyPortal::CScreencopyPortal(zwlr_screencopy_manager_v1* mgr) {
    m_pObject = sdbus::createObject(*g_pPortalManager->getConnection(), OBJECT_PATH);

    m_pObject->registerMethod(INTERFACE_NAME, "CreateSession", "oosa{sv}", "ua{sv}", [&](sdbus::MethodCall c) { onCreateSession(c); });
    m_pObject->registerMethod(INTERFACE_NAME, "SelectSources", "oosa{sv}", "ua{sv}", [&](sdbus::MethodCall c) { onSelectSources(c); });
    m_pObject->registerMethod(INTERFACE_NAME, "Start", "oossa{sv}", "ua{sv}", [&](sdbus::MethodCall c) { onStart(c); });
    m_pObject->registerProperty(INTERFACE_NAME, "AvailableSourceTypes", "u", [](sdbus::PropertyGetReply& reply) -> void { reply << (uint32_t)(VIRTUAL | MONITOR | WINDOW); });
    m_pObject->registerProperty(INTERFACE_NAME, "AvailableCursorModes", "u", [](sdbus::PropertyGetReply& reply) -> void { reply << (uint32_t)(HIDDEN | EMBEDDED); });
    m_pObject->registerProperty(INTERFACE_NAME, "version", "u", [](sdbus::PropertyGetReply& reply) -> void { reply << (uint32_t)(3); });

    m_pObject->finishRegistration();

    m_sState.screencopy = mgr;
    m_pPipewire         = std::make_unique<CPipewireConnection>();

    Debug::log(LOG, "[screencopy] init successful");
}

bool CPipewireConnection::good() {
    return m_pContext && m_pCore;
}

CPipewireConnection::CPipewireConnection() {
    m_pContext = pw_context_new(g_pPortalManager->m_sPipewire.loop, nullptr, 0);

    if (!m_pContext) {
        Debug::log(ERR, "[pipewire] pw didn't allow for a context");
        return;
    }

    m_pCore = pw_context_connect(m_pContext, nullptr, 0);

    if (!m_pCore) {
        Debug::log(ERR, "[pipewire] pw didn't allow for a context connection");
        return;
    }

    Debug::log(LOG, "[pipewire] connected");
}

CPipewireConnection::~CPipewireConnection() {
    if (m_pCore)
        pw_core_disconnect(m_pCore);
    if (m_pContext)
        pw_context_destroy(m_pContext);
}
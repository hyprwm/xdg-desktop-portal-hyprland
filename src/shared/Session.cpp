#include "Session.hpp"
#include "../core/PortalManager.hpp"
#include "../helpers/Log.hpp"

static void onCloseRequest(sdbus::MethodCall& call, SDBusRequest* req) {
    Debug::log(TRACE, "[internal] Close Request {}", (void*)req);

    if (!req)
        return;

    auto r = call.createReply();
    r.send();

    req->onDestroy();
    req->object.release();
}

static void onCloseSession(sdbus::MethodCall& call, SDBusSession* sess) {
    Debug::log(TRACE, "[internal] Close Session {}", (void*)sess);

    if (!sess)
        return;

    auto r = call.createReply();
    r.send();

    sess->onDestroy();
    sess->object.release();
}

std::unique_ptr<SDBusSession> createDBusSession(sdbus::ObjectPath handle) {
    Debug::log(TRACE, "[internal] Create Session {}", handle.c_str());

    std::unique_ptr<SDBusSession> pSession = std::make_unique<SDBusSession>();
    const auto                    PSESSION = pSession.get();

    pSession->object = sdbus::createObject(*g_pPortalManager->getConnection(), handle);

    pSession->object->registerMethod("org.freedesktop.impl.portal.Session", "Close", "", "", [PSESSION](sdbus::MethodCall c) { onCloseSession(c, PSESSION); });

    pSession->object->finishRegistration();

    return pSession;
}

std::unique_ptr<SDBusRequest> createDBusRequest(sdbus::ObjectPath handle) {
    Debug::log(TRACE, "[internal] Create Request {}", handle.c_str());

    std::unique_ptr<SDBusRequest> pRequest = std::make_unique<SDBusRequest>();
    const auto                    PREQUEST = pRequest.get();

    pRequest->object = sdbus::createObject(*g_pPortalManager->getConnection(), handle);

    pRequest->object->registerMethod("org.freedesktop.impl.portal.Request", "Close", "", "", [PREQUEST](sdbus::MethodCall c) { onCloseRequest(c, PREQUEST); });

    pRequest->object->finishRegistration();

    return pRequest;
}
#include "Session.hpp"
#include "../core/PortalManager.hpp"
#include "../helpers/Log.hpp"

static int onCloseRequest(SDBusRequest* req) {
    Debug::log(TRACE, "[internal] Close Request {}", (void*)req);

    if (!req)
        return 0;

    req->onDestroy();
    req->object.release();

    return 0;
}

static int onCloseSession(SDBusSession* sess) {
    Debug::log(TRACE, "[internal] Close Session {}", (void*)sess);

    if (!sess)
        return 0;

    sess->onDestroy();
    sess->object.release();

    return 0;
}

std::unique_ptr<SDBusSession> createDBusSession(sdbus::ObjectPath handle) {
    Debug::log(TRACE, "[internal] Create Session {}", handle.c_str());

    std::unique_ptr<SDBusSession> pSession = std::make_unique<SDBusSession>();
    const auto                    PSESSION = pSession.get();

    pSession->object = sdbus::createObject(*g_pPortalManager->getConnection(), handle);

    pSession->object->addVTable(sdbus::registerMethod("Close").implementedAs([PSESSION]() { onCloseSession(PSESSION); })).forInterface("org.freedesktop.impl.portal.Session");

    return pSession;
}

std::unique_ptr<SDBusRequest> createDBusRequest(sdbus::ObjectPath handle) {
    Debug::log(TRACE, "[internal] Create Request {}", handle.c_str());

    std::unique_ptr<SDBusRequest> pRequest = std::make_unique<SDBusRequest>();
    const auto                    PREQUEST = pRequest.get();

    pRequest->object = sdbus::createObject(*g_pPortalManager->getConnection(), handle);

    pRequest->object->addVTable(sdbus::registerMethod("Close").implementedAs([PREQUEST]() { onCloseRequest(PREQUEST); })).forInterface("org.freedesktop.impl.portal.Request");

    return pRequest;
}
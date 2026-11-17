#include "Session.hpp"
#include "../core/PortalManager.hpp"
#include "../helpers/Log.hpp"
#include <chrono>

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

    // A repeated Close before the deferred destroy runs must not queue a second
    // callback: onDestroy may free sess, leaving that callback dangling.
    if (sess->closing) {
        Debug::log(LOG, "[internal] Close Session {}: already closing, ignored", sess->handle.c_str());
        return 0;
    }

    sess->closing = true;
    Debug::log(LOG, "[internal] Close Session {}: received, destroy deferred", sess->handle.c_str());

    // The teardown waits for the event loop; log how long, so a busy loop (e.g. a
    // synchronous picker) shows up next to the frontend's close timeouts.
    auto destroy = std::move(sess->onDestroy);
    g_pPortalManager->addTimer({0, [sess, destroy, received = std::chrono::steady_clock::now()]() {
                                    const auto DELAY = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - received);
                                    Debug::log(LOG, "[internal] Close Session {}: destroying after {}ms", sess->handle.c_str(), DELAY.count());
                                    sess->object.reset();
                                    if (destroy)
                                        destroy();
                                }});

    return 0;
}

std::unique_ptr<SDBusSession> createDBusSession(sdbus::ObjectPath handle) {
    Debug::log(TRACE, "[internal] Create Session {}", handle.c_str());

    std::unique_ptr<SDBusSession> pSession = std::make_unique<SDBusSession>();
    const auto                    PSESSION = pSession.get();

    pSession->handle = handle;
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
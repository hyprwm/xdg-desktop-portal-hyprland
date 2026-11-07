#include "Clipboard.hpp"
#include "../shared/ClipboardHost.hpp"
#include "../core/PortalManager.hpp"
#include "../helpers/Log.hpp"

#include <algorithm>
#include <fcntl.h>
#include <unistd.h>

static constexpr const char* FAILED_ERROR = "org.freedesktop.DBus.Error.Failed";

// Raise a D-Bus error back to the caller of a method.
[[noreturn]] static void fail(const std::string& message) {
    throw sdbus::Error(sdbus::Error::Name{FAILED_ERROR}, message);
}

static void flushWayland() {
    wl_display_flush(g_pPortalManager->m_sWaylandConnection.display);
}

CClipboardPortal::CClipboardPortal(SP<CCExtDataControlManagerV1> manager, SP<CCWlSeat> seat, std::vector<IClipboardHost*> hosts) : m_manager(manager), m_hosts(std::move(hosts)) {
    if (m_manager && seat) {
        m_device = makeShared<CCExtDataControlDeviceV1>(m_manager->sendGetDataDevice(seat->resource()));

        m_device->setDataOffer([this](CCExtDataControlDeviceV1*, wl_proxy* proxy) { onDataOffer(proxy); });
        m_device->setSelection([this](CCExtDataControlDeviceV1*, wl_proxy* proxy) { onSelection(proxy); });
        m_device->setFinished([this](CCExtDataControlDeviceV1*) {
            Debug::log(WARN, "[clipboard] data control device finished, clipboard sync is unavailable");
            m_device.reset();
        });
    } else
        Debug::log(WARN, "[clipboard] compositor doesn't support ext_data_control_v1, clipboard will be unavailable");

    for (const auto& host : m_hosts) {
        m_listeners.emplace_back(host->m_events.sessionDestroyed.listen([this](sdbus::ObjectPath path) { dropSession(path); }));
        // Tell a session what the clipboard holds as soon as it may use it: it can't have
        // heard about changes made while it was away.
        m_listeners.emplace_back(host->m_events.sessionActivated.listen([this](sdbus::ObjectPath path) { emitOwnerChangedTo(path); }));
    }

    m_pObject = sdbus::createObject(*g_pPortalManager->getConnection(), OBJECT_PATH);

    m_pObject
        ->addVTable(
            sdbus::registerMethod("RequestClipboard").implementedAs([this](sdbus::ObjectPath o, std::unordered_map<std::string, sdbus::Variant> m) { onRequestClipboard(o, m); }),
            sdbus::registerMethod("SetSelection").implementedAs([this](sdbus::ObjectPath o, std::unordered_map<std::string, sdbus::Variant> m) { onSetSelection(o, m); }),
            sdbus::registerMethod("SelectionWrite").implementedAs([this](sdbus::ObjectPath o, uint32_t serial) { return onSelectionWrite(o, serial); }),
            sdbus::registerMethod("SelectionWriteDone").implementedAs([this](sdbus::ObjectPath o, uint32_t serial, bool success) { onSelectionWriteDone(o, serial, success); }),
            sdbus::registerMethod("SelectionRead").implementedAs([this](sdbus::ObjectPath o, std::string mime) { return onSelectionRead(o, mime); }),
            sdbus::registerSignal("SelectionOwnerChanged").withParameters<sdbus::ObjectPath, std::unordered_map<std::string, sdbus::Variant>>(),
            sdbus::registerSignal("SelectionTransfer").withParameters<sdbus::ObjectPath, std::string, uint32_t>(),
            sdbus::registerProperty("version").withGetter([this]() { return version(); }))
        .forInterface(INTERFACE_NAME);

    Debug::log(LOG, "[clipboard] registered");
}

CClipboardPortal::~CClipboardPortal() {
    for (auto& session : m_sessions) {
        closePendingWrites(*session);
        if (session->source)
            session->source->sendDestroy();
    }

    for (auto& announced : m_announcedOffers)
        announced.offer->sendDestroy();

    if (m_selection.offer)
        m_selection.offer->sendDestroy();

    if (m_device)
        m_device->sendDestroy();

    if (m_manager)
        m_manager->sendDestroy();
}

uint32_t CClipboardPortal::version() {
    return 1;
}

// ─── Wayland side ────────────────────────────────────────────────

void CClipboardPortal::onDataOffer(wl_proxy* proxy) {
    const auto OFFER = makeShared<CCExtDataControlOfferV1>(proxy);

    // The offer's mime types follow as events, before the selection that uses it.
    OFFER->setOffer([this, RAW = OFFER.get()](CCExtDataControlOfferV1*, const char* mime) {
        const auto IT = std::ranges::find_if(m_announcedOffers, [RAW](const SOffer& o) { return o.offer.get() == RAW; });
        if (IT == m_announcedOffers.end())
            return;

        IT->mimeTypes.emplace_back(mime);
    });

    m_announcedOffers.push_back(SOffer{.offer = OFFER});
}

void CClipboardPortal::onSelection(wl_proxy* proxy) {
    if (m_selection.offer)
        m_selection.offer->sendDestroy();
    m_selection = {};

    if (proxy) {
        const auto IT = std::ranges::find_if(m_announcedOffers, [proxy](const SOffer& o) { return rc<wl_proxy*>(o.offer->resource()) == proxy; });
        if (IT != m_announcedOffers.end()) {
            m_selection = std::move(*IT);
            m_announcedOffers.erase(IT);
        }
    }

    // Whatever else was announced (a primary selection, say) is of no use to us.
    for (auto& announced : m_announcedOffers)
        announced.offer->sendDestroy();
    m_announcedOffers.clear();

    // Nobody owns the selection but a source of ours, and ours is gone if it's not the
    // compositor's choice any more.
    if (!m_selection.offer)
        m_owner = sdbus::ObjectPath{};

    emitOwnerChanged();
}

void CClipboardPortal::onSourceSend(const sdbus::ObjectPath& handle, const std::string& mimeType, int fd) {
    // We own `fd` from here on: it has to be closed on every path out.
    const auto PSESSION = getSession(handle);
    const auto PHOST    = hostFor(handle);
    if (!PSESSION || !PHOST || !PHOST->clipboardEnabled(handle)) {
        close(fd);
        return;
    }

    const auto SERIAL = ++m_serial;
    PSESSION->pendingWrites.emplace(SERIAL, fd);

    try {
        m_pObject->emitSignal("SelectionTransfer").onInterface(INTERFACE_NAME).withArguments(handle, mimeType, SERIAL);
    } catch (std::exception& e) {
        Debug::log(ERR, "[clipboard] failed to emit SelectionTransfer ({})", e.what());
        close(fd);
        PSESSION->pendingWrites.erase(SERIAL);
    }
}

void CClipboardPortal::onSourceCancelled(const sdbus::ObjectPath& handle, const CCExtDataControlSourceV1* source) {
    // `handle` lives in the closure of the source's own callback, which goes away with
    // the source: take a copy before letting go of it.
    const sdbus::ObjectPath HANDLE   = handle;
    const auto              PSESSION = getSession(HANDLE);

    // A source we replaced ourselves is destroyed before it can be cancelled, so this
    // is another client taking the selection.
    if (!PSESSION || PSESSION->source.get() != source)
        return;

    // The protocol wants a cancelled source destroyed. Keep it alive until we return,
    // as we are running inside one of its events.
    const auto CANCELLED = PSESSION->source;
    PSESSION->source.reset();
    CANCELLED->sendDestroy();

    if (m_owner == HANDLE)
        m_owner = sdbus::ObjectPath{};
}

void CClipboardPortal::emitOwnerChanged() {
    for (const auto& session : m_sessions)
        emitOwnerChangedTo(session->handle);
}

void CClipboardPortal::emitOwnerChangedTo(const sdbus::ObjectPath& path) {
    const auto PHOST = hostFor(path);
    if (!PHOST || !PHOST->clipboardEnabled(path))
        return;

    std::unordered_map<std::string, sdbus::Variant> options;
    options["mime_types"]       = sdbus::Variant{m_selection.mimeTypes};
    options["session_is_owner"] = sdbus::Variant{m_owner == path};

    try {
        m_pObject->emitSignal("SelectionOwnerChanged").onInterface(INTERFACE_NAME).withArguments(path, options);
    } catch (std::exception& e) { Debug::log(ERR, "[clipboard] failed to emit SelectionOwnerChanged ({})", e.what()); }
}

// ─── Sessions ────────────────────────────────────────────────────

IClipboardHost* CClipboardPortal::hostFor(const sdbus::ObjectPath& path) {
    const auto IT = std::ranges::find_if(m_hosts, [&path](IClipboardHost* host) { return host->hasSession(path); });
    return IT != m_hosts.end() ? *IT : nullptr;
}

CClipboardPortal::SSession* CClipboardPortal::getSession(const sdbus::ObjectPath& path) {
    for (auto& session : m_sessions) {
        if (session->handle == path)
            return session.get();
    }

    return nullptr;
}

void CClipboardPortal::closePendingWrites(SSession& session) {
    for (const auto& [serial, fd] : session.pendingWrites)
        close(fd);
    session.pendingWrites.clear();
}

void CClipboardPortal::dropSession(const sdbus::ObjectPath& path) {
    const auto PSESSION = getSession(path);
    if (!PSESSION)
        return;

    closePendingWrites(*PSESSION);
    if (PSESSION->source)
        PSESSION->source->sendDestroy();

    // Destroying a source we own drops the selection with it.
    if (m_owner == path)
        m_owner = sdbus::ObjectPath{};

    std::erase_if(m_sessions, [&](const auto& session) { return session->handle == path; });
    flushWayland();
}

void CClipboardPortal::requireClipboard(const sdbus::ObjectPath& path) {
    if (!m_device)
        fail("The compositor does not support clipboard access");

    const auto PHOST = hostFor(path);
    if (!PHOST || !PHOST->clipboardEnabled(path))
        fail("Clipboard access was not granted for this session");
}

// ─── D-Bus methods ───────────────────────────────────────────────

void CClipboardPortal::onRequestClipboard(sdbus::ObjectPath sessionHandle, std::unordered_map<std::string, sdbus::Variant> opts) {
    Debug::log(LOG, "[clipboard] RequestClipboard for {}", sessionHandle.c_str());

    if (!m_device)
        fail("The compositor does not support clipboard access");

    const auto PHOST = hostFor(sessionHandle);
    if (!PHOST || !PHOST->requestClipboard(sessionHandle))
        fail("Clipboard can only be requested for a RemoteDesktop or InputCapture session that hasn't started");

    if (getSession(sessionHandle))
        return;

    m_sessions.emplace_back(std::make_unique<SSession>(SSession{.handle = sessionHandle}));
}

void CClipboardPortal::onSetSelection(sdbus::ObjectPath sessionHandle, std::unordered_map<std::string, sdbus::Variant> opts) {
    requireClipboard(sessionHandle);

    const auto PSESSION = getSession(sessionHandle);
    if (!PSESSION)
        fail("Unknown session");

    std::vector<std::string> mimeTypes;
    if (const auto IT = opts.find("mime_types"); IT != opts.end()) {
        try {
            mimeTypes = IT->second.get<std::vector<std::string>>();
        } catch (std::exception& e) { fail("mime_types must be an array of strings"); }
    }

    // Replace the previous source: destroying it first means it is never cancelled.
    if (PSESSION->source) {
        PSESSION->source->sendDestroy();
        PSESSION->source.reset();
    }
    closePendingWrites(*PSESSION);

    if (mimeTypes.empty()) {
        // Nothing to offer: give up the selection if it's ours.
        if (m_owner == sessionHandle) {
            m_device->sendSetSelection(nullptr);
            m_owner = sdbus::ObjectPath{};
        }
        flushWayland();
        return;
    }

    const auto SOURCE = makeShared<CCExtDataControlSourceV1>(m_manager->sendCreateDataSource());
    SOURCE->setSend([this, sessionHandle](CCExtDataControlSourceV1*, const char* mime, int32_t fd) { onSourceSend(sessionHandle, mime, fd); });
    SOURCE->setCancelled([this, sessionHandle](CCExtDataControlSourceV1* source) { onSourceCancelled(sessionHandle, source); });

    for (const auto& mime : mimeTypes)
        SOURCE->sendOffer(mime.c_str());

    PSESSION->source = SOURCE;
    m_owner          = sessionHandle;

    m_device->sendSetSelection(SOURCE.get());
    flushWayland();
}

sdbus::UnixFd CClipboardPortal::onSelectionWrite(sdbus::ObjectPath sessionHandle, uint32_t serial) {
    requireClipboard(sessionHandle);

    const auto PSESSION = getSession(sessionHandle);
    if (!PSESSION)
        fail("Unknown session");

    const auto IT = PSESSION->pendingWrites.find(serial);
    if (IT == PSESSION->pendingWrites.end())
        fail("No pending transfer for this serial");

    // The reply carries its own duplicate of the descriptor, so ours can go right away.
    // The reader only sees EOF once every copy of the write end is closed.
    const int FD = IT->second;
    PSESSION->pendingWrites.erase(IT);

    sdbus::UnixFd reply{FD};
    close(FD);
    return reply;
}

void CClipboardPortal::onSelectionWriteDone(sdbus::ObjectPath sessionHandle, uint32_t serial, bool success) {
    requireClipboard(sessionHandle);

    const auto PSESSION = getSession(sessionHandle);
    if (!PSESSION)
        return;

    // A transfer that failed before SelectionWrite was ever called still holds its fd.
    if (const auto IT = PSESSION->pendingWrites.find(serial); IT != PSESSION->pendingWrites.end()) {
        close(IT->second);
        PSESSION->pendingWrites.erase(IT);
    }

    if (!success)
        Debug::log(WARN, "[clipboard] transfer {} for {} failed", serial, sessionHandle.c_str());
}

sdbus::UnixFd CClipboardPortal::onSelectionRead(sdbus::ObjectPath sessionHandle, std::string mimeType) {
    requireClipboard(sessionHandle);

    if (!m_selection.offer)
        fail("The clipboard is empty");

    if (!std::ranges::contains(m_selection.mimeTypes, mimeType))
        fail("The clipboard has no content of this mime type");

    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0)
        fail("Failed to create a pipe");

    m_selection.offer->sendReceive(mimeType.c_str(), fds[1]);
    flushWayland();
    close(fds[1]);

    sdbus::UnixFd reply{fds[0]};
    close(fds[0]);
    return reply;
}

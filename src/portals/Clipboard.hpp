#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <hyprutils/signal/Signal.hpp>
#include <sdbus-c++/sdbus-c++.h>

#include "../includes.hpp"
#include "../dbusDefines.hpp"

#include "ext-data-control-v1.hpp"

class CCWlSeat;
class CRemoteDesktopPortal;

// Implements org.freedesktop.impl.portal.Clipboard. This portal has no sessions of
// its own: it extends RemoteDesktop sessions with access to the compositor clipboard,
// through ext-data-control-v1.
class CClipboardPortal {
  public:
    CClipboardPortal(SP<CCExtDataControlManagerV1> manager, SP<CCWlSeat> seat, CRemoteDesktopPortal* remoteDesktop);
    ~CClipboardPortal();

    void          onRequestClipboard(sdbus::ObjectPath sessionHandle, std::unordered_map<std::string, sdbus::Variant> opts);
    void          onSetSelection(sdbus::ObjectPath sessionHandle, std::unordered_map<std::string, sdbus::Variant> opts);
    sdbus::UnixFd onSelectionWrite(sdbus::ObjectPath sessionHandle, uint32_t serial);
    void          onSelectionWriteDone(sdbus::ObjectPath sessionHandle, uint32_t serial, bool success);
    sdbus::UnixFd onSelectionRead(sdbus::ObjectPath sessionHandle, std::string mimeType);
    uint32_t      version();

  private:
    // The compositor's current selection, as last announced to us.
    struct SOffer {
        SP<CCExtDataControlOfferV1> offer;
        std::vector<std::string>    mimeTypes;
    };

    // What a session set as the selection. At most one session owns it at a time.
    struct SSession {
        sdbus::ObjectPath            handle;
        SP<CCExtDataControlSourceV1> source;
        // Write ends handed to us by the compositor, waiting for the client to ask for them.
        std::unordered_map<uint32_t, int> pendingWrites;
    };

    SSession*                       getSession(const sdbus::ObjectPath& path);
    void                            requireClipboard(const sdbus::ObjectPath& path);
    void                            onDataOffer(wl_proxy* proxy);
    void                            onSelection(wl_proxy* proxy);
    void                            onSourceSend(const sdbus::ObjectPath& handle, const std::string& mimeType, int fd);
    void                            onSourceCancelled(const sdbus::ObjectPath& handle, const CCExtDataControlSourceV1* source);
    void                            emitOwnerChanged();
    void                            dropSession(const sdbus::ObjectPath& path);
    static void                     closePendingWrites(SSession& session);

    std::unique_ptr<sdbus::IObject> m_pObject;
    SP<CCExtDataControlManagerV1>   m_manager;
    SP<CCExtDataControlDeviceV1>    m_device;
    CRemoteDesktopPortal*           m_remoteDesktop = nullptr;

    // Offers announced by dataOffer whose selection hasn't been decided yet.
    std::vector<SOffer>                    m_announcedOffers;
    SOffer                                 m_selection;
    std::vector<std::unique_ptr<SSession>> m_sessions;
    uint32_t                               m_serial = 0;
    // The session whose source is the compositor's current selection, if any.
    sdbus::ObjectPath                      m_owner;

    Hyprutils::Signal::CHyprSignalListener m_sessionDestroyedListener;

    const sdbus::InterfaceName             INTERFACE_NAME = sdbus::InterfaceName{"org.freedesktop.impl.portal.Clipboard"};
    const sdbus::ObjectPath                OBJECT_PATH    = sdbus::ObjectPath{"/org/freedesktop/portal/desktop"};
};

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

#include "../shared/DataControl.hpp"

class IClipboardHost;

// Implements org.freedesktop.impl.portal.Clipboard. This portal has no sessions of
// its own: it extends the sessions of other portals (RemoteDesktop, InputCapture) with
// access to the compositor clipboard, through a data control protocol (ext-data-control-v1, or wlr-data-control).
class CClipboardPortal {
  public:
    CClipboardPortal(std::unique_ptr<IDataControl> dataControl, std::vector<IClipboardHost*> hosts);
    ~CClipboardPortal();

    void          onRequestClipboard(sdbus::ObjectPath sessionHandle, std::unordered_map<std::string, sdbus::Variant> opts);
    void          onSetSelection(sdbus::ObjectPath sessionHandle, std::unordered_map<std::string, sdbus::Variant> opts);
    sdbus::UnixFd onSelectionWrite(sdbus::ObjectPath sessionHandle, uint32_t serial);
    void          onSelectionWriteDone(sdbus::ObjectPath sessionHandle, uint32_t serial, bool success);
    sdbus::UnixFd onSelectionRead(sdbus::ObjectPath sessionHandle, std::string mimeType);
    uint32_t      version();

  private:
    // What a session set as the selection. At most one session owns it at a time.
    struct SSession {
        sdbus::ObjectPath      handle;
        SP<IDataControlSource> source;
        // Write ends handed to us by the compositor, waiting for the client to ask for them.
        std::unordered_map<uint32_t, int> pendingWrites;
    };

    SSession*                       getSession(const sdbus::ObjectPath& path);
    void                            requireClipboard(const sdbus::ObjectPath& path);
    void                            onSelection(SP<IDataControlOffer> offer);
    void                            onSourceSend(const sdbus::ObjectPath& handle, const std::string& mimeType, int fd);
    void                            onSourceCancelled(const sdbus::ObjectPath& handle, const IDataControlSource* source);
    IClipboardHost*                 hostFor(const sdbus::ObjectPath& path);
    void                            emitOwnerChanged();
    void                            emitOwnerChangedTo(const sdbus::ObjectPath& path);
    void                            dropSession(const sdbus::ObjectPath& path);
    bool                            available() const;
    std::vector<std::string>        selectionMimeTypes() const;
    static void                     closePendingWrites(SSession& session);

    std::unique_ptr<sdbus::IObject> m_pObject;
    std::unique_ptr<IDataControl>   m_dataControl;
    // The compositor stopped sending selections.
    bool                         m_finished = false;
    std::vector<IClipboardHost*> m_hosts;

    // The compositor's current selection, if any.
    SP<IDataControlOffer>                  m_selection;
    std::vector<std::unique_ptr<SSession>> m_sessions;
    uint32_t                               m_serial = 0;
    // The session whose source is the compositor's current selection, if any.
    sdbus::ObjectPath                                   m_owner;

    std::vector<Hyprutils::Signal::CHyprSignalListener> m_listeners;

    const sdbus::InterfaceName                          INTERFACE_NAME = sdbus::InterfaceName{"org.freedesktop.impl.portal.Clipboard"};
    const sdbus::ObjectPath                             OBJECT_PATH    = sdbus::ObjectPath{"/org/freedesktop/portal/desktop"};
};

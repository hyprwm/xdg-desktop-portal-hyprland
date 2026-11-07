#pragma once

#include <hyprutils/signal/Signal.hpp>
#include <sdbus-c++/sdbus-c++.h>

// A portal whose sessions can be given access to the clipboard (see CClipboardPortal).
class IClipboardHost {
  public:
    virtual ~IClipboardHost() = default;

    // Whether the session belongs to this portal.
    virtual bool hasSession(const sdbus::ObjectPath& path) = 0;
    // Record that the session asked for the clipboard. Only valid before it starts.
    virtual bool requestClipboard(const sdbus::ObjectPath& path) = 0;
    // The session started, and was granted the clipboard.
    virtual bool clipboardEnabled(const sdbus::ObjectPath& path) = 0;

    struct {
        // Emitted with the session handle once a session is gone.
        Hyprutils::Signal::CSignalT<sdbus::ObjectPath> sessionDestroyed;
        // Emitted with the session handle when a session is worth telling about the clipboard again.
        Hyprutils::Signal::CSignalT<sdbus::ObjectPath> sessionActivated;
    } m_events;
};

#pragma once

#include <sdbus-c++/sdbus-c++.h>
#include "hyprland-global-shortcuts-v1.hpp"
#include "../shared/Session.hpp"
#include "../dbusDefines.hpp"

struct SKeybind {
    SKeybind(SP<CCHyprlandGlobalShortcutV1> shortcut);
    std::string                    id, description, preferredTrigger;
    SP<CCHyprlandGlobalShortcutV1> shortcut = nullptr;
    void*                          session  = nullptr;
};

class CGlobalShortcutsPortal {
  public:
    CGlobalShortcutsPortal(SP<CCHyprlandGlobalShortcutsManagerV1> mgr);

    using DBusShortcut = sdbus::Struct<std::string, std::unordered_map<std::string, sdbus::Variant>>;

    dbUasv onCreateSession(sdbus::ObjectPath requestHandle, sdbus::ObjectPath sessionHandle, std::string appID, std::unordered_map<std::string, sdbus::Variant> opts);
    dbUasv onBindShortcuts(sdbus::ObjectPath requestHandle, sdbus::ObjectPath sessionHandle, std::vector<DBusShortcut> shortcuts, std::string appID,
                           std::unordered_map<std::string, sdbus::Variant> opts);
    dbUasv onListShortcuts(sdbus::ObjectPath sessionHandle, sdbus::ObjectPath requestHandle);

    void   onActivated(SKeybind* pKeybind, uint64_t time);
    void   onDeactivated(SKeybind* pKeybind, uint64_t time);

    struct SSession {
        std::string                            appid;
        sdbus::ObjectPath                      requestHandle, sessionHandle;
        std::unique_ptr<SDBusRequest>          request;
        std::unique_ptr<SDBusSession>          session;

        bool                                   registered = false;

        std::vector<std::unique_ptr<SKeybind>> keybinds;
    };

    std::vector<std::unique_ptr<SSession>> m_vSessions;

  private:
    struct {
        SP<CCHyprlandGlobalShortcutsManagerV1> manager;
    } m_sState;

    std::unique_ptr<sdbus::IObject> m_pObject;

    SSession*                       getSession(sdbus::ObjectPath& path);
    SKeybind*                       getShortcutById(const std::string& appID, const std::string& shortcutId);
    SKeybind*                       registerShortcut(SSession* session, const DBusShortcut& shortcut);

    const sdbus::InterfaceName      INTERFACE_NAME = sdbus::InterfaceName{"org.freedesktop.impl.portal.GlobalShortcuts"};
    const sdbus::ObjectPath         OBJECT_PATH    = sdbus::ObjectPath{"/org/freedesktop/portal/desktop"};
};
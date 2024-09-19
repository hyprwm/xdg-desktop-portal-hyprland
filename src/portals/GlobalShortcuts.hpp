#pragma once

#include <sdbus-c++/sdbus-c++.h>
#include "hyprland-global-shortcuts-v1.hpp"
#include "../shared/Session.hpp"

struct SKeybind {
    SKeybind(SP<CCHyprlandGlobalShortcutV1> shortcut);
    std::string                    id, description, preferredTrigger;
    SP<CCHyprlandGlobalShortcutV1> shortcut = nullptr;
    void*                          session  = nullptr;
};

class CGlobalShortcutsPortal {
  public:
    CGlobalShortcutsPortal(SP<CCHyprlandGlobalShortcutsManagerV1> mgr);

    void onCreateSession(sdbus::MethodCall& call);
    void onBindShortcuts(sdbus::MethodCall& call);
    void onListShortcuts(sdbus::MethodCall& call);

    void onActivated(SKeybind* pKeybind, uint64_t time);
    void onDeactivated(SKeybind* pKeybind, uint64_t time);

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

    using DBusShortcut = sdbus::Struct<std::string, std::unordered_map<std::string, sdbus::Variant>>;

    SSession*         getSession(sdbus::ObjectPath& path);
    SKeybind*         getShortcutById(const std::string& appID, const std::string& shortcutId);
    SKeybind*         registerShortcut(SSession* session, const DBusShortcut& shortcut);

    const std::string INTERFACE_NAME = "org.freedesktop.impl.portal.GlobalShortcuts";
    const std::string OBJECT_PATH    = "/org/freedesktop/portal/desktop";
};
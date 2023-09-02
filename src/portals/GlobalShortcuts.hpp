#pragma once

#include <sdbus-c++/sdbus-c++.h>
#include <protocols/hyprland-global-shortcuts-v1-protocol.h>
#include "../shared/Session.hpp"

struct SKeybind {
    std::string                  id, description, preferredTrigger;
    hyprland_global_shortcut_v1* shortcut = nullptr;
    void*                        session  = nullptr;
};

class CGlobalShortcutsPortal {
  public:
    CGlobalShortcutsPortal(hyprland_global_shortcuts_manager_v1* mgr);

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
        hyprland_global_shortcuts_manager_v1* manager;
    } m_sState;

    std::unique_ptr<sdbus::IObject> m_pObject;

    SSession*                       getSession(sdbus::ObjectPath& path);

    const std::string               INTERFACE_NAME = "org.freedesktop.impl.portal.GlobalShortcuts";
    const std::string               OBJECT_PATH    = "/org/freedesktop/portal/desktop";
};
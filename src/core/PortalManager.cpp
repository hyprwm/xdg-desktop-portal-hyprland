#include "PortalManager.hpp"
#include "../helpers/Log.hpp"

#include <protocols/hyprland-global-shortcuts-v1-protocol.h>
#include <protocols/hyprland-toplevel-export-v1-protocol.h>
#include <protocols/wlr-foreign-toplevel-management-unstable-v1-protocol.h>
#include <protocols/wlr-screencopy-unstable-v1-protocol.h>

void handleGlobal(void* data, struct wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    g_pPortalManager->onGlobal(data, registry, name, interface, version);
}

void handleGlobalRemove(void* data, struct wl_registry* registry, uint32_t name) {
    ; // noop
}

inline const struct wl_registry_listener registryListener = {
    .global        = handleGlobal,
    .global_remove = handleGlobalRemove,
};

//

void CPortalManager::onGlobal(void* data, struct wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    const std::string INTERFACE = interface;

    Debug::log(LOG, " | Got interface: {} (ver {})", INTERFACE, version);

    if (INTERFACE == zwlr_screencopy_manager_v1_interface.name)
        m_sPortals.screencopy = std::make_unique<CScreencopyPortal>((zwlr_screencopy_manager_v1*)wl_registry_bind(registry, name, &zwlr_screencopy_manager_v1_interface, version));
}

void CPortalManager::init() {
    m_pConnection = sdbus::createDefaultBusConnection("org.freedesktop.impl.portal.desktop.hyprland");

    if (!m_pConnection) {
        Debug::log(CRIT, "Couldn't connect to dbus");
        exit(1);
    }

    // init wayland connection
    m_sWaylandConnection.display = wl_display_connect(nullptr);

    if (!m_sWaylandConnection.display) {
        Debug::log(CRIT, "Couldn't connect to a wayland compositor");
        exit(1);
    }

    if (const auto ENV = getenv("XDG_CURRENT_DESKTOP"); ENV) {
        Debug::log(LOG, "XDG_CURRENT_DESKTOP set to {}", ENV);

        if (std::string(ENV) != "Hyprland")
            Debug::log(WARN, "Not running on hyprland, some features might be unavailable");
    } else {
        Debug::log(WARN, "XDG_CURRENT_DESKTOP unset, running on an unknown desktop");
    }

    wl_registry* registry = wl_display_get_registry(m_sWaylandConnection.display);
    wl_registry_add_listener(registry, &registryListener, nullptr);

    Debug::log(LOG, "Gathering exported interfaces");

    wl_display_roundtrip(m_sWaylandConnection.display);

    while (1) {
        // dbus events
        while (m_pConnection->processPendingRequest()) {
            ;
        }

        // wayland events
        while (1) {
            auto r = wl_display_dispatch_pending(m_sWaylandConnection.display);
            wl_display_flush(m_sWaylandConnection.display);

            if (r <= 0)
                break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

sdbus::IConnection* CPortalManager::getConnection() {
    return m_pConnection.get();
}
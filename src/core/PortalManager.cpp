#include "PortalManager.hpp"
#include "../helpers/Log.hpp"

#include <protocols/hyprland-global-shortcuts-v1-protocol.h>
#include <protocols/hyprland-toplevel-export-v1-protocol.h>
#include <protocols/wlr-foreign-toplevel-management-unstable-v1-protocol.h>
#include <protocols/wlr-screencopy-unstable-v1-protocol.h>
#include <protocols/linux-dmabuf-unstable-v1-protocol.h>

#include <pipewire/pipewire.h>
#include <sys/poll.h>

#include <thread>

void handleGlobal(void* data, struct wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    g_pPortalManager->onGlobal(data, registry, name, interface, version);
}

void handleGlobalRemove(void* data, struct wl_registry* registry, uint32_t name) {
    ; // noop
}

inline const wl_registry_listener registryListener = {
    .global        = handleGlobal,
    .global_remove = handleGlobalRemove,
};

static void handleOutputGeometry(void* data, struct wl_output* wl_output, int32_t x, int32_t y, int32_t physical_width, int32_t physical_height, int32_t subpixel, const char* make,
                                 const char* model, int32_t transform) {
    ;
}

static void handleOutputDone(void* data, struct wl_output* wl_output) {
    ;
}

static void handleOutputMode(void* data, struct wl_output* wl_output, uint32_t flags, int32_t width, int32_t height, int32_t refresh) {
    ;
}

static void handleOutputScale(void* data, struct wl_output* wl_output, int32_t factor) {
    ;
}

static void handleOutputName(void* data, struct wl_output* wl_output, const char* name) {
    if (!name)
        return;

    const auto POUTPUT = (SOutput*)data;
    POUTPUT->name      = name;

    Debug::log(LOG, "Found output name {}", POUTPUT->name);
}

static void handleOutputDescription(void* data, struct wl_output* wl_output, const char* description) {
    ;
}

inline const wl_output_listener outputListener = {
    .geometry    = handleOutputGeometry,
    .mode        = handleOutputMode,
    .done        = handleOutputDone,
    .scale       = handleOutputScale,
    .name        = handleOutputName,
    .description = handleOutputDescription,
};

static void handleDMABUFFormat(void* data, struct zwp_linux_dmabuf_v1* zwp_linux_dmabuf_v1, uint32_t format) {
    ;
}

static void handleDMABUFModifier(void* data, struct zwp_linux_dmabuf_v1* zwp_linux_dmabuf_v1, uint32_t format, uint32_t modifier_hi, uint32_t modifier_lo) {
    g_pPortalManager->m_vDMABUFMods.push_back({format, (((uint64_t)modifier_hi) << 32) | modifier_lo});
}

inline const zwp_linux_dmabuf_v1_listener dmabufListener = {
    .format   = handleDMABUFFormat,
    .modifier = handleDMABUFModifier,
};

//

void CPortalManager::onGlobal(void* data, struct wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    const std::string INTERFACE = interface;

    Debug::log(LOG, " | Got interface: {} (ver {})", INTERFACE, version);

    if (INTERFACE == zwlr_screencopy_manager_v1_interface.name && m_sPipewire.loop)
        m_sPortals.screencopy = std::make_unique<CScreencopyPortal>((zwlr_screencopy_manager_v1*)wl_registry_bind(registry, name, &zwlr_screencopy_manager_v1_interface, version));

    else if (INTERFACE == hyprland_toplevel_export_manager_v1_interface.name)
        m_sWaylandConnection.hyprlandToplevelMgr = wl_registry_bind(registry, name, &hyprland_toplevel_export_manager_v1_interface, version);

    else if (INTERFACE == wl_output_interface.name) {
        const auto POUTPUT = m_vOutputs.emplace_back(std::make_unique<SOutput>()).get();
        POUTPUT->output    = (wl_output*)wl_registry_bind(registry, name, &wl_output_interface, version);
        wl_output_add_listener(POUTPUT->output, &outputListener, POUTPUT);
        POUTPUT->id = name;
    }

    else if (INTERFACE == zwp_linux_dmabuf_v1_interface.name) {
        if (version < 4) {
            Debug::log(ERR, "cannot use linux_dmabuf with ver < 4");
            return;
        }

        m_sWaylandConnection.linuxDmabuf         = wl_registry_bind(registry, name, &zwp_linux_dmabuf_v1_interface, version);
        m_sWaylandConnection.linuxDmabufFeedback = zwp_linux_dmabuf_v1_get_default_feedback((zwp_linux_dmabuf_v1*)m_sWaylandConnection.linuxDmabuf);
        // TODO: dmabuf
    }

    else if (INTERFACE == wl_shm_interface.name)
        m_sWaylandConnection.shm = (wl_shm*)wl_registry_bind(registry, name, &wl_shm_interface, version);
}

void CPortalManager::onGlobalRemoved(void* data, struct wl_registry* registry, uint32_t name) {
    std::erase_if(m_vOutputs, [&](const auto& other) { return other->id == name; });
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

    pw_init(nullptr, nullptr);
    m_sPipewire.loop = pw_loop_new(nullptr);

    if (!m_sPipewire.loop)
        Debug::log(ERR, "Pipewire: refused to create a loop. Screensharing will not work.");

    Debug::log(LOG, "Gathering exported interfaces");

    wl_display_roundtrip(m_sWaylandConnection.display);

    if (!m_sPortals.screencopy)
        Debug::log(WARN, "Screencopy not started: compositor doesn't support zwlr_screencopy_v1 or pw refused a loop");
    else if (m_sWaylandConnection.hyprlandToplevelMgr)
        m_sPortals.screencopy->appendToplevelExport(m_sWaylandConnection.hyprlandToplevelMgr);

    wl_display_roundtrip(m_sWaylandConnection.display);

    while (1) {
        // dbus events
        m_mEventLock.lock();

        while (m_pConnection->processPendingRequest()) {
            ;
        }

        std::vector<CTimer*> toRemove;
        for (auto& t : m_vTimers) {
            if (t->passed()) {
                t->m_fnCallback();
                toRemove.emplace_back(t.get());
                Debug::log(TRACE, "[core] calling timer {}", (void*)t.get());
            }
        }

        while (pw_loop_iterate(m_sPipewire.loop, 0) != 0) {
            ;
        }

        wl_display_flush(m_sWaylandConnection.display);
        if (wl_display_prepare_read(m_sWaylandConnection.display) == 0) {
            wl_display_read_events(m_sWaylandConnection.display);
            wl_display_dispatch_pending(m_sWaylandConnection.display);
        } else {
            wl_display_dispatch(m_sWaylandConnection.display);
        }

        if (!toRemove.empty())
            std::erase_if(m_vTimers,
                          [&](const auto& t) { return std::find_if(toRemove.begin(), toRemove.end(), [&](const auto& other) { return other == t.get(); }) != toRemove.end(); });

        m_mEventLock.unlock();

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

sdbus::IConnection* CPortalManager::getConnection() {
    return m_pConnection.get();
}

SOutput* CPortalManager::getOutputFromName(const std::string& name) {
    for (auto& o : m_vOutputs) {
        if (o->name == name)
            return o.get();
    }
    return nullptr;
}
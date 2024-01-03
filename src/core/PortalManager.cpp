#include "PortalManager.hpp"
#include "../helpers/Log.hpp"
#include "../helpers/MiscFunctions.hpp"

#include <protocols/hyprland-global-shortcuts-v1-protocol.h>
#include <protocols/hyprland-toplevel-export-v1-protocol.h>
#include <protocols/wlr-foreign-toplevel-management-unstable-v1-protocol.h>
#include <protocols/wlr-screencopy-unstable-v1-protocol.h>
#include <protocols/linux-dmabuf-unstable-v1-protocol.h>

#include <pipewire/pipewire.h>
#include <sys/poll.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

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
    const auto POUTPUT = (SOutput*)data;

    POUTPUT->transform = (wl_output_transform)transform;
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

static void dmabufFeedbackMainDevice(void* data, zwp_linux_dmabuf_feedback_v1* feedback, wl_array* device_arr) {
    Debug::log(LOG, "[core] dmabufFeedbackMainDevice");

    RASSERT(!g_pPortalManager->m_sWaylandConnection.gbm, "double dmabuf feedback");

    dev_t device;
    assert(device_arr->size == sizeof(device));
    memcpy(&device, device_arr->data, sizeof(device));

    drmDevice* drmDev;
    if (drmGetDeviceFromDevId(device, /* flags */ 0, &drmDev) != 0) {
        Debug::log(WARN, "[dmabuf] unable to open main device?");
        exit(1);
    }

    g_pPortalManager->m_sWaylandConnection.gbmDevice = g_pPortalManager->createGBMDevice(drmDev);
}

static void dmabufFeedbackFormatTable(void* data, zwp_linux_dmabuf_feedback_v1* feedback, int fd, uint32_t size) {
    Debug::log(TRACE, "[core] dmabufFeedbackFormatTable");

    g_pPortalManager->m_vDMABUFMods.clear();

    g_pPortalManager->m_sWaylandConnection.dma.formatTable = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);

    if (g_pPortalManager->m_sWaylandConnection.dma.formatTable == MAP_FAILED) {
        Debug::log(ERR, "[core] format table failed to mmap");
        g_pPortalManager->m_sWaylandConnection.dma.formatTable     = nullptr;
        g_pPortalManager->m_sWaylandConnection.dma.formatTableSize = 0;
        return;
    }

    g_pPortalManager->m_sWaylandConnection.dma.formatTableSize = size;
}

static void dmabufFeedbackDone(void* data, zwp_linux_dmabuf_feedback_v1* feedback) {
    Debug::log(TRACE, "[core] dmabufFeedbackDone");

    if (g_pPortalManager->m_sWaylandConnection.dma.formatTable)
        munmap(g_pPortalManager->m_sWaylandConnection.dma.formatTable, g_pPortalManager->m_sWaylandConnection.dma.formatTableSize);

    g_pPortalManager->m_sWaylandConnection.dma.formatTable     = nullptr;
    g_pPortalManager->m_sWaylandConnection.dma.formatTableSize = 0;
}

static void dmabufFeedbackTrancheTargetDevice(void* data, zwp_linux_dmabuf_feedback_v1* feedback, wl_array* device_arr) {
    Debug::log(TRACE, "[core] dmabufFeedbackTrancheTargetDevice");

    dev_t device;
    assert(device_arr->size == sizeof(device));
    memcpy(&device, device_arr->data, sizeof(device));

    drmDevice* drmDev;
    if (drmGetDeviceFromDevId(device, /* flags */ 0, &drmDev) != 0)
        return;

    if (g_pPortalManager->m_sWaylandConnection.gbmDevice) {
        drmDevice* drmDevRenderer = NULL;
        drmGetDevice2(gbm_device_get_fd(g_pPortalManager->m_sWaylandConnection.gbmDevice), /* flags */ 0, &drmDevRenderer);
        g_pPortalManager->m_sWaylandConnection.dma.deviceUsed = drmDevicesEqual(drmDevRenderer, drmDev);
    } else {
        g_pPortalManager->m_sWaylandConnection.gbmDevice      = g_pPortalManager->createGBMDevice(drmDev);
        g_pPortalManager->m_sWaylandConnection.dma.deviceUsed = g_pPortalManager->m_sWaylandConnection.gbm;
    }
}

static void dmabufFeedbackTrancheFlags(void* data, zwp_linux_dmabuf_feedback_v1* feedback, uint32_t flags) {
    ;
}

static void dmabufFeedbackTrancheFormats(void* data, zwp_linux_dmabuf_feedback_v1* feedback, wl_array* indices) {
    Debug::log(TRACE, "[core] dmabufFeedbackTrancheFormats");

    if (!g_pPortalManager->m_sWaylandConnection.dma.deviceUsed || !g_pPortalManager->m_sWaylandConnection.dma.formatTable)
        return;

    struct fm_entry {
        uint32_t format;
        uint32_t padding;
        uint64_t modifier;
    };
    // An entry in the table has to be 16 bytes long
    assert(sizeof(struct fm_entry) == 16);

    uint32_t  n_modifiers = g_pPortalManager->m_sWaylandConnection.dma.formatTableSize / sizeof(struct fm_entry);
    fm_entry* fm_entry    = (struct fm_entry*)g_pPortalManager->m_sWaylandConnection.dma.formatTable;
    uint16_t* idx;

    for (idx = (uint16_t*)indices->data; (const char*)idx < (const char*)indices->data + indices->size; idx++) {
        if (*idx >= n_modifiers)
            continue;

        g_pPortalManager->m_vDMABUFMods.push_back({(fm_entry + *idx)->format, (fm_entry + *idx)->modifier});
    }
}

static void dmabufFeedbackTrancheDone(void* data, struct zwp_linux_dmabuf_feedback_v1* zwp_linux_dmabuf_feedback_v1) {
    Debug::log(TRACE, "[core] dmabufFeedbackTrancheDone");

    g_pPortalManager->m_sWaylandConnection.dma.deviceUsed = false;
}

inline const zwp_linux_dmabuf_feedback_v1_listener dmabufFeedbackListener = {
    .done                  = dmabufFeedbackDone,
    .format_table          = dmabufFeedbackFormatTable,
    .main_device           = dmabufFeedbackMainDevice,
    .tranche_done          = dmabufFeedbackTrancheDone,
    .tranche_target_device = dmabufFeedbackTrancheTargetDevice,
    .tranche_formats       = dmabufFeedbackTrancheFormats,
    .tranche_flags         = dmabufFeedbackTrancheFlags,
};

//

CPortalManager::CPortalManager() {
    const auto XDG_CONFIG_HOME = getenv("XDG_CONFIG_HOME");
    const auto HOME            = getenv("HOME");

    if (!HOME && !XDG_CONFIG_HOME) {
        Debug::log(CRIT, "Cannot proceed: neither $HOME nor $XDG_CONFIG_HOME is present in env");
        throw "$HOME and $XDG_CONFIG_HOME both missing from env";
    }

    std::string path = XDG_CONFIG_HOME ? std::string{XDG_CONFIG_HOME} + "/hypr/xdph.conf" : std::string{HOME} + "/.config/hypr/xdph.conf";

    m_sConfig.config = std::make_unique<Hyprlang::CConfig>(path.c_str(), Hyprlang::SConfigOptions{.allowMissingConfig = true});

    m_sConfig.config->addConfigValue("general:toplevel_dynamic_bind", {0L});

    m_sConfig.config->commence();
    m_sConfig.config->parse();
}

void CPortalManager::onGlobal(void* data, struct wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    const std::string INTERFACE = interface;

    Debug::log(LOG, " | Got interface: {} (ver {})", INTERFACE, version);

    if (INTERFACE == zwlr_screencopy_manager_v1_interface.name && m_sPipewire.loop)
        m_sPortals.screencopy = std::make_unique<CScreencopyPortal>((zwlr_screencopy_manager_v1*)wl_registry_bind(registry, name, &zwlr_screencopy_manager_v1_interface, version));

    if (INTERFACE == hyprland_global_shortcuts_manager_v1_interface.name)
        m_sPortals.globalShortcuts = std::make_unique<CGlobalShortcutsPortal>(
            (hyprland_global_shortcuts_manager_v1*)wl_registry_bind(registry, name, &hyprland_global_shortcuts_manager_v1_interface, version));

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
        zwp_linux_dmabuf_feedback_v1_add_listener((zwp_linux_dmabuf_feedback_v1*)m_sWaylandConnection.linuxDmabufFeedback, &dmabufFeedbackListener, nullptr);
    }

    else if (INTERFACE == wl_shm_interface.name)
        m_sWaylandConnection.shm = (wl_shm*)wl_registry_bind(registry, name, &wl_shm_interface, version);

    else if (INTERFACE == zwlr_foreign_toplevel_manager_v1_interface.name) {
        m_sHelpers.toplevel = std::make_unique<CToplevelManager>(registry, name, version);

        // remove when another fix is found for https://github.com/hyprwm/xdg-desktop-portal-hyprland/issues/147
        if (!std::any_cast<Hyprlang::INT>(m_sConfig.config->getConfigValue("general:toplevel_dynamic_bind")))
            m_sHelpers.toplevel->activate();
    }
}

void CPortalManager::onGlobalRemoved(void* data, struct wl_registry* registry, uint32_t name) {
    std::erase_if(m_vOutputs, [&](const auto& other) { return other->id == name; });
}

void CPortalManager::init() {
    m_iPID = getpid();

    try {
        m_pConnection = sdbus::createSessionBusConnection("org.freedesktop.impl.portal.desktop.hyprland");
    } catch (std::exception& e) {
        Debug::log(CRIT, "Couldn't create the dbus connection ({})", e.what());
        exit(1);
    }

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

    if (!inShellPath("grim"))
        Debug::log(WARN, "grim not found. Screenshots will not work.");
    else {
        m_sPortals.screenshot = std::make_unique<CScreenshotPortal>();

        if (!inShellPath("slurp"))
            Debug::log(WARN, "slurp not found. You won't be able to select a region when screenshotting.");

        if (!inShellPath("slurp") && !inShellPath("hyprpicker"))
            Debug::log(WARN, "Neither slurp nor hyprpicker found. You won't be able to pick colors.");
        else if (!inShellPath("hyprpicker"))
            Debug::log(INFO, "hyprpicker not found. We suggest to use hyprpicker for color picking to be less meh.");
    }

    wl_display_roundtrip(m_sWaylandConnection.display);

    startEventLoop();
}

void CPortalManager::startEventLoop() {

    pollfd pollfds[] = {
        {
            .fd     = m_pConnection->getEventLoopPollData().fd,
            .events = POLLIN,
        },
        {
            .fd     = wl_display_get_fd(m_sWaylandConnection.display),
            .events = POLLIN,
        },
        {
            .fd     = pw_loop_get_fd(m_sPipewire.loop),
            .events = POLLIN,
        },
    };

    std::thread pollThr([this, &pollfds]() {
        while (1) {
            int ret = poll(pollfds, 3, 5000 /* 5 seconds, reasonable. It's because we might need to terminate */);
            if (ret < 0) {
                Debug::log(CRIT, "[core] Polling fds failed with {}", strerror(errno));
                g_pPortalManager->terminate();
            }

            for (size_t i = 0; i < 3; ++i) {
                if (pollfds[i].revents & POLLHUP) {
                    Debug::log(CRIT, "[core] Disconnected from pollfd id {}", i);
                    g_pPortalManager->terminate();
                }
            }

            if (m_bTerminate)
                break;

            if (ret != 0) {
                Debug::log(TRACE, "[core] got poll event");
                std::lock_guard<std::mutex> lg(m_sEventLoopInternals.loopRequestMutex);
                m_sEventLoopInternals.shouldProcess = true;
                m_sEventLoopInternals.loopSignal.notify_all();
            }
        }
    });

    m_sTimersThread.thread = std::make_unique<std::thread>([this] {
        while (1) {
            std::unique_lock lk(m_sTimersThread.loopMutex);

            // find nearest timer ms
            m_mEventLock.lock();
            float nearest = 60000; /* reasonable timeout */
            for (auto& t : m_sTimersThread.timers) {
                float until = t->duration() - t->passedMs();
                if (until < nearest)
                    nearest = until;
            }
            m_mEventLock.unlock();

            m_sTimersThread.loopSignal.wait_for(lk, std::chrono::milliseconds((int)nearest), [this] { return m_sTimersThread.shouldProcess; });
            m_sTimersThread.shouldProcess = false;

            if (m_bTerminate)
                break;

            // awakened. Check if any timers passed
            m_mEventLock.lock();
            bool notify = false;
            for (auto& t : m_sTimersThread.timers) {
                if (t->passed()) {
                    Debug::log(TRACE, "[core] got timer event");
                    notify = true;
                    break;
                }
            }
            m_mEventLock.unlock();

            if (notify) {
                std::lock_guard<std::mutex> lg(m_sEventLoopInternals.loopRequestMutex);
                m_sEventLoopInternals.shouldProcess = true;
                m_sEventLoopInternals.loopSignal.notify_all();
            }
        }
    });

    while (1) { // dbus events
        // wait for being awakened
        m_sEventLoopInternals.loopRequestMutex.unlock(); // unlock, we are ready to take events

        std::unique_lock lk(m_sEventLoopInternals.loopMutex);
        if (m_sEventLoopInternals.shouldProcess == false) // avoid a lock if a thread managed to request something already since we .unlock()ed
            m_sEventLoopInternals.loopSignal.wait(lk, [this] { return m_sEventLoopInternals.shouldProcess == true; }); // wait for events

        m_sEventLoopInternals.loopRequestMutex.lock(); // lock incoming events

        if (m_bTerminate)
            break;

        m_sEventLoopInternals.shouldProcess = false;

        m_mEventLock.lock();

        if (pollfds[0].revents & POLLIN /* dbus */) {
            while (m_pConnection->processPendingRequest()) {
                ;
            }
        }

        if (pollfds[1].revents & POLLIN /* wl */) {
            wl_display_flush(m_sWaylandConnection.display);
            if (wl_display_prepare_read(m_sWaylandConnection.display) == 0) {
                wl_display_read_events(m_sWaylandConnection.display);
                wl_display_dispatch_pending(m_sWaylandConnection.display);
            } else {
                wl_display_dispatch(m_sWaylandConnection.display);
            }
        }

        if (pollfds[2].revents & POLLIN /* pw */) {
            while (pw_loop_iterate(m_sPipewire.loop, 0) != 0) {
                ;
            }
        }

        std::vector<CTimer*> toRemove;
        for (auto& t : m_sTimersThread.timers) {
            if (t->passed()) {
                t->m_fnCallback();
                toRemove.emplace_back(t.get());
                Debug::log(TRACE, "[core] calling timer {}", (void*)t.get());
            }
        }

        // finalize wayland dispatching. Dispatch pending on the queue
        int ret = 0;
        do {
            ret = wl_display_dispatch_pending(m_sWaylandConnection.display);
            wl_display_flush(m_sWaylandConnection.display);
        } while (ret > 0);

        if (!toRemove.empty())
            std::erase_if(m_sTimersThread.timers,
                          [&](const auto& t) { return std::find_if(toRemove.begin(), toRemove.end(), [&](const auto& other) { return other == t.get(); }) != toRemove.end(); });

        m_mEventLock.unlock();
    }

    Debug::log(ERR, "[core] Terminated");

    m_sPortals.globalShortcuts.reset();
    m_sPortals.screencopy.reset();

    m_pConnection.reset();
    pw_loop_destroy(m_sPipewire.loop);
    wl_display_disconnect(m_sWaylandConnection.display);

    m_sTimersThread.thread.release();
    pollThr.join(); // wait for poll to exit
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

static char* gbm_find_render_node(drmDevice* device) {
    drmDevice* devices[64];
    char*      render_node = NULL;

    int        n = drmGetDevices2(0, devices, sizeof(devices) / sizeof(devices[0]));
    for (int i = 0; i < n; ++i) {
        drmDevice* dev = devices[i];
        if (device && !drmDevicesEqual(device, dev)) {
            continue;
        }
        if (!(dev->available_nodes & (1 << DRM_NODE_RENDER)))
            continue;

        render_node = strdup(dev->nodes[DRM_NODE_RENDER]);
        break;
    }

    drmFreeDevices(devices, n);
    return render_node;
}

gbm_device* CPortalManager::createGBMDevice(drmDevice* dev) {
    char* renderNode = gbm_find_render_node(dev);

    if (!renderNode) {
        Debug::log(ERR, "[core] Couldn't find a render node");
        return nullptr;
    }

    Debug::log(TRACE, "[core] createGBMDevice: render node {}", renderNode);

    int fd = open(renderNode, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        Debug::log(ERR, "[core] couldn't open render node");
        free(renderNode);
        return NULL;
    }

    free(renderNode);
    return gbm_create_device(fd);
}

void CPortalManager::addTimer(const CTimer& timer) {
    Debug::log(TRACE, "[core] adding timer for {}ms", timer.duration());
    m_sTimersThread.timers.emplace_back(std::make_unique<CTimer>(timer));
    m_sTimersThread.shouldProcess = true;
    m_sTimersThread.loopSignal.notify_all();
}

void CPortalManager::terminate() {
    m_bTerminate = true;

    // if we don't exit in 5s, we'll kill by force. Nuclear option. PIDs are not reused in linux until a wrap-around,
    // and I doubt anyone will make 4.2M PIDs within 5s.
    if (fork() == 0)
        execl("/bin/sh", "/bin/sh", "-c", std::format("sleep 5 && kill -9 {}", m_iPID).c_str(), nullptr);

    {
        m_sEventLoopInternals.shouldProcess = true;
        m_sEventLoopInternals.loopSignal.notify_all();
    }

    m_sTimersThread.shouldProcess = true;
    m_sTimersThread.loopSignal.notify_all();
}

#pragma once

#include <memory>
#include <sdbus-c++/sdbus-c++.h>
#include <wayland-client.h>
#include <hyprlang.hpp>

#include "../portals/Screencopy.hpp"
#include "../portals/Screenshot.hpp"
#include "../portals/GlobalShortcuts.hpp"
#include "../helpers/Timer.hpp"
#include "../shared/ToplevelManager.hpp"
#include <gbm.h>
#include <xf86drm.h>

#include <mutex>

struct pw_loop;

struct SOutput {
    std::string         name;
    wl_output*          output    = nullptr;
    uint32_t            id        = 0;
    wl_output_transform transform = WL_OUTPUT_TRANSFORM_NORMAL;
};

struct SDMABUFModifier {
    uint32_t fourcc = 0;
    uint64_t mod    = 0;
};

class CPortalManager {
  public:
    CPortalManager();

    void                init();

    void                onGlobal(void* data, struct wl_registry* registry, uint32_t name, const char* interface, uint32_t version);
    void                onGlobalRemoved(void* data, struct wl_registry* registry, uint32_t name);

    sdbus::IConnection* getConnection();
    SOutput*            getOutputFromName(const std::string& name);

    struct {
        pw_loop* loop = nullptr;
    } m_sPipewire;

    struct {
        std::unique_ptr<CScreencopyPortal>      screencopy;
        std::unique_ptr<CScreenshotPortal>      screenshot;
        std::unique_ptr<CGlobalShortcutsPortal> globalShortcuts;
    } m_sPortals;

    struct {
        std::unique_ptr<CToplevelManager> toplevel;
    } m_sHelpers;

    struct {
        wl_display* display             = nullptr;
        void*       hyprlandToplevelMgr = nullptr;
        void*       linuxDmabuf         = nullptr;
        void*       linuxDmabufFeedback = nullptr;
        wl_shm*     shm                 = nullptr;
        gbm_bo*     gbm;
        gbm_device* gbmDevice;
        struct {
            void*  formatTable     = nullptr;
            size_t formatTableSize = 0;
            bool   deviceUsed      = false;
        } dma;
    } m_sWaylandConnection;

    struct {
        std::unique_ptr<Hyprlang::CConfig> config;
    } m_sConfig;

    std::vector<SDMABUFModifier> m_vDMABUFMods;

    void                         addTimer(const CTimer& timer);

    gbm_device*                  createGBMDevice(drmDevice* dev);

    // terminate after the event loop has been created. Before we can exit()
    void terminate();

  private:
    void  startEventLoop();

    bool  m_bTerminate = false;
    pid_t m_iPID       = 0;

    struct {
        std::condition_variable loopSignal;
        std::mutex              loopMutex;
        std::atomic<bool>       shouldProcess = false;
        std::mutex              loopRequestMutex;
    } m_sEventLoopInternals;

    struct {
        std::condition_variable              loopSignal;
        std::mutex                           loopMutex;
        bool                                 shouldProcess = false;
        std::vector<std::unique_ptr<CTimer>> timers;
        std::unique_ptr<std::thread>         thread;
    } m_sTimersThread;

    std::unique_ptr<sdbus::IConnection>   m_pConnection;
    std::vector<std::unique_ptr<SOutput>> m_vOutputs;

    std::mutex                            m_mEventLock;
};

inline std::unique_ptr<CPortalManager> g_pPortalManager;
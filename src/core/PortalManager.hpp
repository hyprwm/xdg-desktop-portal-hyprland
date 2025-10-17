#pragma once

#include <memory>
#include <sdbus-c++/sdbus-c++.h>
#include <hyprlang.hpp>

#include "wayland.hpp"
#include "../portals/Screencopy.hpp"
#include "../portals/Screenshot.hpp"
#include "../portals/GlobalShortcuts.hpp"
#include "../portals/InputCapture.hpp"
#include "../portals/RemoteDesktop.hpp"
#include "../helpers/Timer.hpp"
#include "../shared/ToplevelManager.hpp"
#include "../shared/ToplevelMappingManager.hpp"
#include <gbm.h>
#include <poll.h>
#include <xf86drm.h>

#include "hyprland-toplevel-export-v1.hpp"
#include "hyprland-global-shortcuts-v1.hpp"
#include "linux-dmabuf-v1.hpp"
#include "wlr-foreign-toplevel-management-unstable-v1.hpp"
#include "wlr-screencopy-unstable-v1.hpp"
#include "wlr-virtual-pointer-unstable-v1.hpp"
#include "virtual-keyboard-unstable-v1.hpp"

#include "../includes.hpp"
#include "../dbusDefines.hpp"

#include <mutex>

struct pw_loop;

struct SOutput {
    SOutput(SP<CCWlOutput>);
    std::string         name;
    SP<CCWlOutput>      output      = nullptr;
    uint32_t            id          = 0;
    float               refreshRate = 60.0;
    wl_output_transform transform   = WL_OUTPUT_TRANSFORM_NORMAL;
    uint32_t            width       = 0;
    uint32_t            height      = 0;
    int32_t             x           = 0;
    int32_t             y           = 0;
    int32_t             scale       = 1;
};

struct SDMABUFModifier {
    uint32_t fourcc = 0;
    uint64_t mod    = 0;
};

class CPortalManager {
  public:
    CPortalManager();

    void                                         init();

    void                                         onGlobal(uint32_t name, const char* interface, uint32_t version);
    void                                         onGlobalRemoved(uint32_t name);

    sdbus::IConnection*                          getConnection();
    SOutput*                                     getOutputFromName(const std::string& name);
    std::vector<std::unique_ptr<SOutput>> const& getAllOutputs();

    struct {
        enum wl_keyboard_keymap_format format;
        int32_t                        fd;
        uint32_t                       size;
    } m_sKeymap;

    struct {
        pw_loop* loop = nullptr;
    } m_sPipewire;

    struct {
        std::unique_ptr<CScreencopyPortal>      screencopy;
        std::unique_ptr<CScreenshotPortal>      screenshot;
        std::unique_ptr<CGlobalShortcutsPortal> globalShortcuts;
        std::unique_ptr<CInputCapturePortal>    inputCapture;
        std::unique_ptr<CRemoteDesktopPortal>   remoteDesktop;
    } m_sPortals;

    struct {
        std::unique_ptr<CToplevelManager>        toplevel;
        std::unique_ptr<CToplevelMappingManager> toplevelMapping;
    } m_sHelpers;

    struct {
        wl_display*                           display = nullptr;
        SP<CCWlSeat>                          seat;
        SP<CCWlRegistry>                      registry;
        SP<CCHyprlandToplevelExportManagerV1> hyprlandToplevelMgr;
        SP<CCZwpLinuxDmabufV1>                linuxDmabuf;
        SP<CCZwpLinuxDmabufFeedbackV1>        linuxDmabufFeedback;
        SP<CCWlShm>                           shm;
        gbm_bo*                               gbm       = nullptr;
        gbm_device*                           gbmDevice = nullptr;
        struct {
            void*  formatTable     = nullptr;
            size_t formatTableSize = 0;
            bool   deviceUsed      = false;
            bool   done            = false;
        } dma;
    } m_sWaylandConnection;

    struct {
        std::unique_ptr<Hyprlang::CConfig> config;
    } m_sConfig;

    std::vector<SDMABUFModifier> m_vDMABUFMods;

    void                         addTimer(const CTimer& timer);

    gbm_device*                  createGBMDevice(drmDevice* dev);

    void                         addFdToEventLoop(int fd, short events, std::function<void()> callback);
    void                         removeFdFromEventLoop(int fd);

    // terminate after the event loop has been created. Before we can exit()
    void terminate();

  private:
    void  startEventLoop();

    bool  m_bTerminate = false;
    pid_t m_iPID       = 0;

    struct {
        std::condition_variable              loopSignal;
        std::mutex                           loopMutex;
        std::atomic<bool>                    shouldProcess = false;
        std::mutex                           loopRequestMutex;
        std::vector<pollfd>                  pollFds;
        std::map<int, std::function<void()>> pollCallbacks;
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

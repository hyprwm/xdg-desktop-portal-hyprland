#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <sdbus-c++/sdbus-c++.h>
#include <hyprlang.hpp>

#include "wayland.hpp"
#include "../portals/Screencopy.hpp"
#include "../portals/Screenshot.hpp"
#include "../portals/GlobalShortcuts.hpp"
#include "../portals/InputCapture.hpp"
#include "../shared/ToplevelManager.hpp"
#include "../shared/ToplevelMappingManager.hpp"
#include "../shared/WorkspaceTracker.hpp"
#include <gbm.h>
#include <xf86drm.h>

#include "hyprland-toplevel-export-v1.hpp"
#include "hyprland-global-shortcuts-v1.hpp"
#include "linux-dmabuf-v1.hpp"
#include "wlr-foreign-toplevel-management-unstable-v1.hpp"
#include "wlr-screencopy-unstable-v1.hpp"

#include "../includes.hpp"
#include "../dbusDefines.hpp"

#include <hyprutils/eventLoop/EventMask.hpp>

struct pw_loop;

namespace Hyprutils::EventLoop {
    class IEventLoop;
    class IFDSource;
    class IPostDispatchHook;
    class ITimer;
}

class CCZxdgOutputManagerV1;
class CCZxdgOutputV1;
class CCExtImageCopyCaptureManagerV1;
class CCHyprlandWorkspaceImageCaptureSourceManagerV1;

struct SOutput {
    SOutput(SP<CCWlOutput>);
    std::string         name;
    SP<CCWlOutput>      output               = nullptr;
    SP<CCZxdgOutputV1>  xdgOutput            = nullptr;
    uint32_t            id                   = 0;
    float               refreshRate          = 60.0;
    wl_output_transform transform            = WL_OUTPUT_TRANSFORM_NORMAL;
    uint32_t            width                = 0;
    uint32_t            height               = 0;
    int32_t             x                    = 0;
    int32_t             y                    = 0;
    double              scale                = 1.0;
    int32_t             logicalX             = 0;
    int32_t             logicalY             = 0;
    int32_t             logicalWidth         = 0;
    int32_t             logicalHeight        = 0;
    bool                logicalPositionValid = false;
    bool                logicalSizeValid     = false;
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
        pw_loop* loop = nullptr;
    } m_sPipewire;

    struct {
        std::unique_ptr<CScreencopyPortal>      screencopy;
        std::unique_ptr<CScreenshotPortal>      screenshot;
        std::unique_ptr<CGlobalShortcutsPortal> globalShortcuts;
        std::unique_ptr<CInputCapturePortal>    inputCapture;
    } m_sPortals;

    struct {
        std::unique_ptr<CToplevelManager>        toplevel;
        std::unique_ptr<CToplevelMappingManager> toplevelMapping;
        std::unique_ptr<CWorkspaceTracker>       workspaceTracker;
    } m_sHelpers;

    struct {
        wl_display*                                        display = nullptr;
        SP<CCWlRegistry>                                   registry;
        SP<CCHyprlandToplevelExportManagerV1>              hyprlandToplevelMgr;
        SP<CCExtImageCopyCaptureManagerV1>                 icc;
        SP<CCHyprlandWorkspaceImageCaptureSourceManagerV1> workspaceSource;
        SP<CCZwpLinuxDmabufV1>                             linuxDmabuf;
        SP<CCZwpLinuxDmabufFeedbackV1>                     linuxDmabufFeedback;
        SP<CCZxdgOutputManagerV1>                          xdgOutputManager;
        SP<CCWlShm>                                        shm;
        gbm_bo*                                            gbm       = nullptr;
        gbm_device*                                        gbmDevice = nullptr;
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

    void                         addTimer(std::chrono::milliseconds timeout, std::function<void()>&& callback);
    // runs once the current event loop callback has returned
    void                         addIdle(std::function<void()>&& callback);

    gbm_device*                  createGBMDevice(drmDevice* dev);

    // terminate after the event loop has been created. Before we can exit()
    void terminate();

    void releaseWayland();

  private:
    void                                startEventLoop();
    SP<Hyprutils::EventLoop::IFDSource> addCoreSource(int fd, const char* name, std::function<void(Hyprutils::EventLoop::FdEventMask)>&& callback);
    bool                                processWaylandEvents(bool read);
    void                                processDBusEvents();
    void                                updateDBusPollData();
    void                                setupXDGOutput(SOutput* output);

    bool                                m_bTerminate = false;

    struct {
        SP<Hyprutils::EventLoop::IEventLoop>          loop;
        SP<Hyprutils::EventLoop::IFDSource>           dbus;
        SP<Hyprutils::EventLoop::IFDSource>           dbusEvent;
        SP<Hyprutils::EventLoop::ITimer>              dbusTimeout;
        SP<Hyprutils::EventLoop::IFDSource>           wayland;
        SP<Hyprutils::EventLoop::IFDSource>           pipewire;
        SP<Hyprutils::EventLoop::IPostDispatchHook>   waylandFlush;
        SP<Hyprutils::EventLoop::IPostDispatchHook>   dbusPoll;
        std::vector<SP<Hyprutils::EventLoop::ITimer>> timers;
        bool                                          waylandWantsWrite = false;
        bool                                          dbusWantsWrite    = false;
        std::chrono::microseconds                     dbusDeadline      = std::chrono::microseconds::max();
    } m_sEventLoop;

    std::unique_ptr<sdbus::IConnection>   m_pConnection;
    std::vector<std::unique_ptr<SOutput>> m_vOutputs;
};

inline std::unique_ptr<CPortalManager> g_pPortalManager;

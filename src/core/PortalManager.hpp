#pragma once

#include <memory>
#include <sdbus-c++/sdbus-c++.h>
#include <wayland-client.h>

#include "../portals/Screencopy.hpp"
#include "../helpers/Timer.hpp"

#include <mutex>

struct pw_loop;

struct SOutput {
    std::string name;
    wl_output*  output = nullptr;
    uint32_t    id     = 0;
};

struct SDMABUFModifier {
    uint32_t fourcc = 0;
    uint64_t mod    = 0;
};

class CPortalManager {
  public:
    void                init();

    void                onGlobal(void* data, struct wl_registry* registry, uint32_t name, const char* interface, uint32_t version);
    void                onGlobalRemoved(void* data, struct wl_registry* registry, uint32_t name);

    sdbus::IConnection* getConnection();
    SOutput*            getOutputFromName(const std::string& name);

    struct {
        pw_loop* loop = nullptr;
    } m_sPipewire;

    struct {
        std::unique_ptr<CScreencopyPortal> screencopy;
    } m_sPortals;

    struct {
        wl_display* display             = nullptr;
        void*       hyprlandToplevelMgr = nullptr;
        void*       linuxDmabuf         = nullptr;
        void*       linuxDmabufFeedback = nullptr;
        wl_shm*     shm                 = nullptr;
    } m_sWaylandConnection;

    std::vector<SDMABUFModifier>         m_vDMABUFMods;

    std::vector<std::unique_ptr<CTimer>> m_vTimers;

  private:
    std::unique_ptr<sdbus::IConnection>   m_pConnection;
    std::vector<std::unique_ptr<SOutput>> m_vOutputs;

    std::mutex                            m_mEventLock;
};

inline std::unique_ptr<CPortalManager> g_pPortalManager;
#pragma once

#include <memory>
#include <sdbus-c++/sdbus-c++.h>
#include <wayland-client.h>

#include "../portals/Screencopy.hpp"

class CPortalManager {
  public:
    void                init();

    void                onGlobal(void* data, struct wl_registry* registry, uint32_t name, const char* interface, uint32_t version);

    sdbus::IConnection* getConnection();

  private:
    std::unique_ptr<sdbus::IConnection> m_pConnection;

    struct {
        wl_display* display = nullptr;
    } m_sWaylandConnection;

    struct {
        std::unique_ptr<CScreencopyPortal> screencopy;
    } m_sPortals;
};

inline std::unique_ptr<CPortalManager> g_pPortalManager;
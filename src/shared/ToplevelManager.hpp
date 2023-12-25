#pragma once

#include <wayland-client.h>
#include <protocols/wlr-foreign-toplevel-management-unstable-v1-protocol.h>
#include <string>
#include <vector>
#include <memory>

class CToplevelManager;

struct SToplevelHandle {
    std::string                      windowClass;
    std::string                      windowTitle;
    zwlr_foreign_toplevel_handle_v1* handle = nullptr;
    CToplevelManager*                mgr    = nullptr;
};

class CToplevelManager {
  public:
    CToplevelManager(wl_registry* registry, uint32_t name, uint32_t version);

    void                                          activate();
    void                                          deactivate();

    bool                                          exists(zwlr_foreign_toplevel_handle_v1* handle);

    std::vector<std::unique_ptr<SToplevelHandle>> m_vToplevels;

  private:
    zwlr_foreign_toplevel_manager_v1* m_pManager = nullptr;

    int64_t                           m_iActivateLocks = 0;

    struct {
        wl_registry* registry = nullptr;
        uint32_t     name     = 0;
        uint32_t     version  = 0;
    } m_sWaylandConnection;
};
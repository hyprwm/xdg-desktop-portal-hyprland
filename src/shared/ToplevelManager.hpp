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
    CToplevelManager(zwlr_foreign_toplevel_manager_v1* mgr);

    std::vector<std::unique_ptr<SToplevelHandle>> m_vToplevels;

  private:
    zwlr_foreign_toplevel_manager_v1* m_pManager;
};
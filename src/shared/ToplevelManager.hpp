#pragma once

#include "wayland.hpp"
#include "wlr-foreign-toplevel-management-unstable-v1.hpp"
#include <string>
#include <vector>
#include <memory>
#include "../includes.hpp"

class CToplevelManager;

struct SToplevelHandle {
    SToplevelHandle(SP<CCZwlrForeignToplevelHandleV1> handle);
    std::string                       windowClass;
    std::string                       windowTitle;
    SP<CCZwlrForeignToplevelHandleV1> handle = nullptr;
    CToplevelManager*                 mgr    = nullptr;
};

class CToplevelManager {
  public:
    CToplevelManager(uint32_t name, uint32_t version);

    void                             activate();
    void                             deactivate();
    SP<SToplevelHandle>              handleFromClass(const std::string& windowClass);
    SP<SToplevelHandle>              handleFromHandleLower(uint32_t handle);

    std::vector<SP<SToplevelHandle>> m_vToplevels;

  private:
    SP<CCZwlrForeignToplevelManagerV1> m_pManager = nullptr;

    int64_t                            m_iActivateLocks = 0;

    struct {
        uint32_t name    = 0;
        uint32_t version = 0;
    } m_sWaylandConnection;

    friend struct SToplevelHandle;
};
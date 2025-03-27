#pragma once

#include "wayland.hpp"
#include "hyprland-toplevel-mapping-v1.hpp"
#include "wlr-foreign-toplevel-management-unstable-v1.hpp"
#include <memory>
#include "../includes.hpp"

class CToplevelMappingManager {
  public:
    CToplevelMappingManager(SP<CCHyprlandToplevelMappingManagerV1> mgr);

    uint64_t getWindowForToplevel(SP<CCZwlrForeignToplevelHandleV1> handle);

  private:
    SP<CCHyprlandToplevelMappingManagerV1>                          m_pManager = nullptr;

    std::unordered_map<SP<CCZwlrForeignToplevelHandleV1>, uint64_t> m_mapAddresses;
    void                                                            fetchWindowForToplevel(SP<CCZwlrForeignToplevelHandleV1> handle);

    friend struct SToplevelHandle;
    friend class CToplevelManager;
};
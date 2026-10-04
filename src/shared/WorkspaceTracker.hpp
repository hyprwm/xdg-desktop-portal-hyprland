#pragma once

#include <vector>
#include "../includes.hpp"

#include "ext-workspace-v1.hpp"

class CWorkspaceTracker;

class CTrackedWorkspace {
  public:
    CTrackedWorkspace(CWorkspaceTracker& p, SP<CCExtWorkspaceHandleV1> handle);

    std::string m_name, m_id;

  private:
    SP<CCExtWorkspaceHandleV1> m_handle;
    CWorkspaceTracker&         m_parent;
};

class CWorkspaceTracker {
  public:
    CWorkspaceTracker(SP<CCExtWorkspaceManagerV1> mgr);

    const std::vector<SP<CTrackedWorkspace>>& workspaces() const;

  private:
    SP<CCExtWorkspaceManagerV1>        m_manager;

    std::vector<SP<CTrackedWorkspace>> m_workspaces;

    friend class CTrackedWorkspace;
};

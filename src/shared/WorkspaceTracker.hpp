#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "../includes.hpp"

#include "ext-workspace-v1.hpp"

class CWorkspaceTracker;
class CCWlOutput;
struct STrackedWorkspaceGroup;

class CTrackedWorkspace {
  public:
    CTrackedWorkspace(CWorkspaceTracker& p, SP<CCExtWorkspaceHandleV1> handle);

    std::vector<SP<CCWlOutput>> outputs() const;

    std::string                 m_name, m_id;
    const uint64_t              m_trackerId;
    bool                        m_removed = false;
    SP<CCExtWorkspaceHandleV1>  m_handle;

  private:
    CWorkspaceTracker&         m_parent;
    std::string                m_pendingName, m_pendingId;
    WP<STrackedWorkspaceGroup> m_group, m_pendingGroup;

    friend class CWorkspaceTracker;
};

class CWorkspaceTracker {
  public:
    CWorkspaceTracker(SP<CCExtWorkspaceManagerV1> mgr);
    ~CWorkspaceTracker();

    const std::vector<SP<CTrackedWorkspace>>& workspaces() const;
    SP<CTrackedWorkspace>                     fromName(const std::string_view name) const;
    SP<CTrackedWorkspace>                     fromId(uint64_t id) const;

  private:
    void                                    clear();
    void                                    addGroup(wl_proxy* group);

    SP<CCExtWorkspaceManagerV1>             m_manager;
    std::vector<SP<STrackedWorkspaceGroup>> m_groups;

    std::vector<SP<CTrackedWorkspace>>      m_workspaces;
    std::vector<SP<CTrackedWorkspace>>      m_pendingWorkspaces;
    uint64_t                                m_nextId = 1;

    friend class CTrackedWorkspace;
};

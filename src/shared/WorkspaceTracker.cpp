#include "WorkspaceTracker.hpp"
#include "ext-workspace-v1.hpp"
#include "../helpers/Log.hpp"

CTrackedWorkspace::CTrackedWorkspace(CWorkspaceTracker& p, SP<CCExtWorkspaceHandleV1> handle) : m_handle(handle), m_parent(p) {
    m_handle->setId([this](CCExtWorkspaceHandleV1* r, const char* id) { m_id = id; });
    m_handle->setName([this](CCExtWorkspaceHandleV1* r, const char* name) { m_name = name; });

    m_handle->setRemoved([this](CCExtWorkspaceHandleV1* r) { std::erase_if(m_parent.m_workspaces, [this](const auto& e) { return !e || e.get() == this; }); });
}

CWorkspaceTracker::CWorkspaceTracker(SP<CCExtWorkspaceManagerV1> mgr) : m_manager(mgr) {
    mgr->setWorkspace([this](CCExtWorkspaceManagerV1* r, wl_proxy* ws) {
        Debug::log(TRACE, "[WorkspaceTracker] new workspace {}", (uintptr_t)ws);
        m_workspaces.emplace_back(makeShared<CTrackedWorkspace>(*this, makeShared<CCExtWorkspaceHandleV1>(ws)));
    });
}

const std::vector<SP<CTrackedWorkspace>>& CWorkspaceTracker::workspaces() const {
    return m_workspaces;
}

#include "WorkspaceTracker.hpp"
#include "ext-workspace-v1.hpp"
#include "../core/PortalManager.hpp"
#include "../helpers/Log.hpp"

#include <algorithm>

struct STrackedWorkspaceGroup {
    SP<CCExtWorkspaceGroupHandleV1> handle;
    std::vector<WP<CCWlOutput>>     outputs, pendingOutputs;
};

CTrackedWorkspace::CTrackedWorkspace(CWorkspaceTracker& p, SP<CCExtWorkspaceHandleV1> handle) : m_trackerId(p.m_nextId++), m_handle(handle), m_parent(p) {
    m_handle->setId([this](CCExtWorkspaceHandleV1* r, const char* id) { m_pendingId = id; });
    m_handle->setName([this](CCExtWorkspaceHandleV1* r, const char* name) { m_pendingName = name; });

    m_handle->setRemoved([this](CCExtWorkspaceHandleV1* r) {
        // Keep the callback's owner alive while erasing the last references to this record.
        const auto HANDLE = m_handle;
        auto&      parent = m_parent;
        const auto SELF   = this;
        m_removed         = true;
        m_handle.reset();
        std::erase_if(parent.m_workspaces, [SELF](const auto& ws) { return ws.get() == SELF; });
        std::erase_if(parent.m_pendingWorkspaces, [SELF](const auto& ws) { return ws.get() == SELF; });
    });
}

CWorkspaceTracker::CWorkspaceTracker(SP<CCExtWorkspaceManagerV1> mgr) : m_manager(mgr) {
    mgr->setWorkspaceGroup([this](CCExtWorkspaceManagerV1* r, wl_proxy* group) { addGroup(group); });
    mgr->setWorkspace([this](CCExtWorkspaceManagerV1* r, wl_proxy* ws) {
        Debug::log(TRACE, "[WorkspaceTracker] new workspace {}", rc<uintptr_t>(ws));
        m_pendingWorkspaces.emplace_back(makeShared<CTrackedWorkspace>(*this, makeShared<CCExtWorkspaceHandleV1>(ws)));
    });
    mgr->setDone([this](CCExtWorkspaceManagerV1* r) {
        for (const auto& group : m_groups) {
            group->outputs = group->pendingOutputs;
        }
        for (const auto& ws : m_pendingWorkspaces) {
            ws->m_name  = ws->m_pendingName;
            ws->m_id    = ws->m_pendingId;
            ws->m_group = ws->m_pendingGroup;
        }
        m_workspaces = m_pendingWorkspaces;
    });
    mgr->setFinished([this](CCExtWorkspaceManagerV1* r) { clear(); });
}

std::vector<SP<CCWlOutput>> CTrackedWorkspace::outputs() const {
    std::vector<SP<CCWlOutput>> result;
    const auto                  GROUP = m_group.lock();
    if (!GROUP)
        return result;

    for (const auto& output : GROUP->outputs) {
        if (const auto OUTPUT = output.lock())
            result.emplace_back(OUTPUT);
    }
    return result;
}

void CWorkspaceTracker::addGroup(wl_proxy* group) {
    const auto GROUP = makeShared<STrackedWorkspaceGroup>();
    GROUP->handle    = makeShared<CCExtWorkspaceGroupHandleV1>(group);
    m_groups.emplace_back(GROUP);
    const WP<STrackedWorkspaceGroup> WEAK = GROUP;

    GROUP->handle->setOutputEnter([WEAK](CCExtWorkspaceGroupHandleV1* r, wl_proxy* output) {
        const auto GROUP = WEAK.lock();
        if (!GROUP)
            return;

        for (const auto& candidate : g_pPortalManager->getAllOutputs()) {
            if (candidate->output->proxy() != output)
                continue;
            if (std::ranges::none_of(GROUP->pendingOutputs, [&](const auto& current) { return current.lock() == candidate->output; }))
                GROUP->pendingOutputs.emplace_back(candidate->output);
            break;
        }
    });
    GROUP->handle->setOutputLeave([WEAK](CCExtWorkspaceGroupHandleV1* r, wl_proxy* output) {
        const auto GROUP = WEAK.lock();
        if (!GROUP)
            return;

        std::erase_if(GROUP->pendingOutputs, [output](const auto& current) {
            const auto OUTPUT = current.lock();
            return !OUTPUT || OUTPUT->proxy() == output;
        });
    });
    GROUP->handle->setWorkspaceEnter([this, WEAK](CCExtWorkspaceGroupHandleV1* r, wl_proxy* workspace) {
        for (const auto& ws : m_pendingWorkspaces) {
            if (!ws->m_handle || ws->m_handle->proxy() != workspace)
                continue;
            ws->m_pendingGroup = WEAK;
            break;
        }
    });
    GROUP->handle->setWorkspaceLeave([this, WEAK](CCExtWorkspaceGroupHandleV1* r, wl_proxy* workspace) {
        for (const auto& ws : m_pendingWorkspaces) {
            if (!ws->m_handle || ws->m_handle->proxy() != workspace)
                continue;
            if (ws->m_pendingGroup.lock() == WEAK.lock())
                ws->m_pendingGroup.reset();
            break;
        }
    });
    GROUP->handle->setRemoved([this, WEAK](CCExtWorkspaceGroupHandleV1* r) {
        // Keep the callback's owner alive until dispatch completes. Workspace
        // references expire even if no output_leave or done event follows.
        const auto GROUP = WEAK.lock();
        std::erase(m_groups, GROUP);
    });
}

CWorkspaceTracker::~CWorkspaceTracker() {
    m_manager->setWorkspaceGroup({});
    m_manager->setWorkspace({});
    m_manager->setDone({});
    m_manager->setFinished({});
    clear();
}

void CWorkspaceTracker::clear() {
    m_groups.clear();
    for (const auto& ws : m_pendingWorkspaces) {
        ws->m_removed = true;
        ws->m_handle.reset();
    }
    m_workspaces.clear();
    m_pendingWorkspaces.clear();
}

const std::vector<SP<CTrackedWorkspace>>& CWorkspaceTracker::workspaces() const {
    return m_workspaces;
}

SP<CTrackedWorkspace> CWorkspaceTracker::fromName(const std::string_view name) const {
    SP<CTrackedWorkspace> result;
    for (const auto& w : m_workspaces) {
        if (w->m_removed || w->m_name != name)
            continue;
        if (result)
            return nullptr;
        result = w;
    }

    return result;
}

SP<CTrackedWorkspace> CWorkspaceTracker::fromId(uint64_t id) const {
    for (const auto& w : m_workspaces) {
        if (!w->m_removed && w->m_trackerId == id)
            return w;
    }
    return nullptr;
}

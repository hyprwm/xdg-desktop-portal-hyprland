#include "ToplevelManager.hpp"
#include "../helpers/Log.hpp"
#include "../core/PortalManager.hpp"

SToplevelHandle::SToplevelHandle(SP<CCZwlrForeignToplevelHandleV1> handle_) : handle(handle_) {
    handle->setTitle([this](CCZwlrForeignToplevelHandleV1* r, const char* title) {
        if (title)
            windowTitle = title;

        Debug::log(TRACE, "[toplevel] toplevel at {} set title to {}", (void*)this, windowTitle);
    });
    handle->setAppId([this](CCZwlrForeignToplevelHandleV1* r, const char* class_) {
        if (class_)
            windowClass = class_;

        Debug::log(TRACE, "[toplevel] toplevel at {} set class to {}", (void*)this, windowClass);
    });
    handle->setClosed([this](CCZwlrForeignToplevelHandleV1* r) {
        Debug::log(TRACE, "[toplevel] toplevel at {} closed", (void*)this);

        std::erase_if(g_pPortalManager->m_sHelpers.toplevel->m_vToplevels, [&](const auto& e) { return e.get() == this; });
    });
}

CToplevelManager::CToplevelManager(uint32_t name, uint32_t version) {
    m_sWaylandConnection = {name, version};
}

void CToplevelManager::activate() {
    m_iActivateLocks++;

    Debug::log(LOG, "[toplevel] (activate) locks: {}", m_iActivateLocks);

    if (m_pManager || m_iActivateLocks < 1)
        return;

    m_pManager =
        makeShared<CCZwlrForeignToplevelManagerV1>((wl_proxy*)wl_registry_bind((wl_registry*)g_pPortalManager->m_sWaylandConnection.registry->resource(), m_sWaylandConnection.name,
                                                                               &zwlr_foreign_toplevel_manager_v1_interface, m_sWaylandConnection.version));

    m_pManager->setToplevel([this](CCZwlrForeignToplevelManagerV1* r, wl_proxy* newHandle) {
        Debug::log(TRACE, "[toplevel] New toplevel at {}", (void*)newHandle);

        m_vToplevels.emplace_back(makeShared<SToplevelHandle>(makeShared<CCZwlrForeignToplevelHandleV1>(newHandle)));
    });
    m_pManager->setFinished([this](CCZwlrForeignToplevelManagerV1* r) { m_vToplevels.clear(); });

    wl_display_roundtrip(g_pPortalManager->m_sWaylandConnection.display);

    Debug::log(LOG, "[toplevel] Activated, bound to {:x}, toplevels: {}", (uintptr_t)m_pManager, m_vToplevels.size());
}

void CToplevelManager::deactivate() {
    m_iActivateLocks--;

    Debug::log(LOG, "[toplevel] (deactivate) locks: {}", m_iActivateLocks);

    if (!m_pManager || m_iActivateLocks > 0)
        return;

    m_pManager.reset();
    m_vToplevels.clear();

    Debug::log(LOG, "[toplevel] unbound manager");
}

SP<SToplevelHandle> CToplevelManager::handleFromClass(const std::string& windowClass) {
    for (auto& tl : m_vToplevels) {
        if (tl->windowClass == windowClass)
            return tl;
    }

    return nullptr;
}

SP<SToplevelHandle> CToplevelManager::handleFromHandleLower(uint32_t handle) {
    for (auto& tl : m_vToplevels) {
        if (((uint64_t)tl->handle->resource() & 0xFFFFFFFF) == handle)
            return tl;
    }

    return nullptr;
}
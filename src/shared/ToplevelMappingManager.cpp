#include "ToplevelMappingManager.hpp"
#include "../helpers/Log.hpp"
#include "../core/PortalManager.hpp"

CToplevelMappingManager::CToplevelMappingManager(SP<CCHyprlandToplevelMappingManagerV1> mgr) : m_pManager(mgr) {
    Debug::log(LOG, "[toplevel mapping] registered manager");
}

void CToplevelMappingManager::fetchWindowForToplevel(SP<CCZwlrForeignToplevelHandleV1> handle) {
    if (!handle)
        return;

    Debug::log(TRACE, "[toplevel mapping] fetching window for toplevel at {}", (void*)handle.get());
    auto const HANDLE = makeShared<CCHyprlandToplevelWindowMappingHandleV1>(m_pManager->sendGetWindowForToplevelWlr(handle->resource()));

    m_vHandles.push_back(HANDLE);

    HANDLE->setWindowAddress([handle](CCHyprlandToplevelWindowMappingHandleV1* h, uint32_t address_hi, uint32_t address) {
        const auto ADDRESS = (uint64_t)address_hi << 32 | address;
        g_pPortalManager->m_sHelpers.toplevelMapping->m_muAddresses.insert_or_assign(handle, ADDRESS);
        Debug::log(TRACE, "[toplevel mapping] mapped toplevel at {} to window {}", (void*)handle.get(), ADDRESS);
        std::erase_if(g_pPortalManager->m_sHelpers.toplevelMapping->m_vHandles, [&](const auto& other) { return other.get() == h; });
    });

    HANDLE->setFailed([handle](CCHyprlandToplevelWindowMappingHandleV1* h) {
        Debug::log(TRACE, "[toplevel mapping] failed to map toplevel at {} to window", (void*)handle.get());
        std::erase_if(g_pPortalManager->m_sHelpers.toplevelMapping->m_vHandles, [&](const auto& other) { return other.get() == h; });
    });
}

uint64_t CToplevelMappingManager::getWindowForToplevel(CSharedPointer<CCZwlrForeignToplevelHandleV1> handle) {
    auto iter = m_muAddresses.find(handle);
    if (iter != m_muAddresses.end())
        return iter->second;

    if (handle)
        Debug::log(TRACE, "[toplevel mapping] did not find window address for toplevel at {}", (void*)handle.get());

    return 0;
}

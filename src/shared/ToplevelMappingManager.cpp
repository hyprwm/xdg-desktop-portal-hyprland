#include "ToplevelMappingManager.hpp"
#include "../helpers/Log.hpp"
#include "../core/PortalManager.hpp"

CToplevelMappingManager::CToplevelMappingManager(SP<CCHyprlandToplevelMappingManagerV1> mgr) : m_pManager(mgr) {}

void CToplevelMappingManager::fetchWindowForToplevel(SP<CCZwlrForeignToplevelHandleV1> handle) {
    Debug::log(LOG, "Fetching window for handle");
    auto const HANDLE = makeShared<CCHyprlandToplevelWindowMappingHandleV1>(m_pManager->sendGetWindowForToplevelWlr(handle->resource()));

    if (HANDLE)
        Debug::log(WARN, "Handle exists");
    else
        Debug::log(WARN, "Handle does not exist");

    HANDLE->setWindowAddress([this, handle](CCHyprlandToplevelWindowMappingHandleV1* r, uint32_t address_hi, uint32_t address) {
        Debug::log(WARN, "Got window address {} {}", address_hi, address);
        this->m_mapAddresses.insert_or_assign(handle, (uint64_t)address_hi << 32 | address);
    });

    HANDLE->setFailed([handle](CCHyprlandToplevelWindowMappingHandleV1* r) {
        Debug::log(ERR, "Failed to get window address");
        if (handle)
            Debug::log(TRACE, "[toplevel mapping] failed to map toplevel at {} to window", (void*)handle.get());
    });
}

uint64_t CToplevelMappingManager::getWindowForToplevel(CSharedPointer<CCZwlrForeignToplevelHandleV1> handle) {
    auto iter = m_mapAddresses.find(handle);
    if (iter != m_mapAddresses.end())
        return iter->second;

    if (handle)
        Debug::log(TRACE, "[toplevel mapping] did not find window address for toplevel at {}", (void*)handle.get());

    return 0;
}

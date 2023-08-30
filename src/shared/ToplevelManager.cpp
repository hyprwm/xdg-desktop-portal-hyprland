#include "ToplevelManager.hpp"
#include "../helpers/Log.hpp"

static void toplevelTitle(void* data, zwlr_foreign_toplevel_handle_v1* zwlr_foreign_toplevel_handle_v1, const char* title) {
    const auto PTL = (SToplevelHandle*)data;

    if (title)
        PTL->windowTitle = title;

    Debug::log(TRACE, "[toplevel] toplevel at {} set title to {}", data, PTL->windowTitle);
}

static void toplevelAppid(void* data, zwlr_foreign_toplevel_handle_v1* zwlr_foreign_toplevel_handle_v1, const char* app_id) {
    const auto PTL = (SToplevelHandle*)data;

    if (app_id)
        PTL->windowClass = app_id;

    Debug::log(TRACE, "[toplevel] toplevel at {} set class to {}", data, PTL->windowClass);
}

static void toplevelEnterOutput(void* data, zwlr_foreign_toplevel_handle_v1* zwlr_foreign_toplevel_handle_v1, wl_output* output) {
    ;
}

static void toplevelLeaveOutput(void* data, zwlr_foreign_toplevel_handle_v1* zwlr_foreign_toplevel_handle_v1, wl_output* output) {
    ;
}

static void toplevelState(void* data, zwlr_foreign_toplevel_handle_v1* zwlr_foreign_toplevel_handle_v1, wl_array* state) {
    ;
}

static void toplevelDone(void* data, zwlr_foreign_toplevel_handle_v1* zwlr_foreign_toplevel_handle_v1) {
    ;
}

static void toplevelClosed(void* data, zwlr_foreign_toplevel_handle_v1* zwlr_foreign_toplevel_handle_v1) {
    const auto PTL = (SToplevelHandle*)data;

    std::erase_if(PTL->mgr->m_vToplevels, [&](const auto& e) { return e.get() == PTL; });

    Debug::log(TRACE, "[toplevel] toplevel at {} closed", data);
}

static void toplevelParent(void* data, zwlr_foreign_toplevel_handle_v1* zwlr_foreign_toplevel_handle_v1, struct zwlr_foreign_toplevel_handle_v1* parent) {
    ;
}

inline const zwlr_foreign_toplevel_handle_v1_listener toplevelListener = {
    .title        = toplevelTitle,
    .app_id       = toplevelAppid,
    .output_enter = toplevelEnterOutput,
    .output_leave = toplevelLeaveOutput,
    .state        = toplevelState,
    .done         = toplevelDone,
    .closed       = toplevelClosed,
    .parent       = toplevelParent,
};

static void managerToplevel(void* data, zwlr_foreign_toplevel_manager_v1* mgr, zwlr_foreign_toplevel_handle_v1* toplevel) {
    const auto PMGR = (CToplevelManager*)data;

    Debug::log(TRACE, "[toplevel] New toplevel at {}", (void*)toplevel);

    const auto PTL = PMGR->m_vToplevels.emplace_back(std::make_unique<SToplevelHandle>("?", "?", toplevel, PMGR)).get();

    zwlr_foreign_toplevel_handle_v1_add_listener(toplevel, &toplevelListener, PTL);
}

static void managerFinished(void* data, zwlr_foreign_toplevel_manager_v1* mgr) {
    const auto PMGR = (CToplevelManager*)data;

    Debug::log(ERR, "[toplevel] Compositor sent .finished???");

    PMGR->m_vToplevels.clear();
}

inline const zwlr_foreign_toplevel_manager_v1_listener managerListener = {
    .toplevel = managerToplevel,
    .finished = managerFinished,
};

CToplevelManager::CToplevelManager(zwlr_foreign_toplevel_manager_v1* mgr) {
    m_pManager = mgr;
    zwlr_foreign_toplevel_manager_v1_add_listener(mgr, &managerListener, this);
}

bool CToplevelManager::exists(zwlr_foreign_toplevel_handle_v1* handle) {
    for (auto& h : m_vToplevels) {
        if (h->handle == handle)
            return true;
    }

    return false;
}
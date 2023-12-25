#include "Screencopy.hpp"
#include "../core/PortalManager.hpp"
#include "../helpers/Log.hpp"
#include "../helpers/MiscFunctions.hpp"

#include <libdrm/drm_fourcc.h>
#include <pipewire/pipewire.h>
#include <protocols/linux-dmabuf-unstable-v1-protocol.h>

// --------------- Wayland Protocol Handlers --------------- //

static void wlrOnBuffer(void* data, zwlr_screencopy_frame_v1* frame, uint32_t format, uint32_t width, uint32_t height, uint32_t stride) {
    const auto PSESSION = (CScreencopyPortal::SSession*)data;

    Debug::log(TRACE, "[sc] wlrOnBuffer for {}", (void*)PSESSION);

    PSESSION->sharingData.frameInfoSHM.w      = width;
    PSESSION->sharingData.frameInfoSHM.h      = height;
    PSESSION->sharingData.frameInfoSHM.fmt    = drmFourccFromSHM((wl_shm_format)format);
    PSESSION->sharingData.frameInfoSHM.size   = stride * height;
    PSESSION->sharingData.frameInfoSHM.stride = stride;

    // todo: done if ver < 3
}

static void wlrOnFlags(void* data, zwlr_screencopy_frame_v1* frame, uint32_t flags) {
    const auto PSESSION = (CScreencopyPortal::SSession*)data;

    Debug::log(TRACE, "[sc] wlrOnFlags for {}", (void*)PSESSION);

    // todo: maybe check for y invert?
}

static void wlrOnReady(void* data, zwlr_screencopy_frame_v1* frame, uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec) {
    const auto PSESSION = (CScreencopyPortal::SSession*)data;

    Debug::log(TRACE, "[sc] wlrOnReady for {}", (void*)PSESSION);

    PSESSION->sharingData.status = FRAME_READY;

    PSESSION->sharingData.tvSec         = ((((uint64_t)tv_sec_hi) << 32) + (uint64_t)tv_sec_lo);
    PSESSION->sharingData.tvNsec        = tv_nsec;
    PSESSION->sharingData.tvTimestampNs = PSESSION->sharingData.tvSec * SPA_NSEC_PER_SEC + PSESSION->sharingData.tvNsec;

    Debug::log(TRACE, "[sc] frame timestamp sec: {} nsec: {} combined: {}ns", PSESSION->sharingData.tvSec, PSESSION->sharingData.tvNsec, PSESSION->sharingData.tvTimestampNs);

    g_pPortalManager->m_sPortals.screencopy->m_pPipewire->enqueue(PSESSION);

    if (g_pPortalManager->m_sPortals.screencopy->m_pPipewire->streamFromSession(PSESSION))
        g_pPortalManager->m_sPortals.screencopy->queueNextShareFrame(PSESSION);

    zwlr_screencopy_frame_v1_destroy(frame);
    PSESSION->sharingData.frameCallback = nullptr;
}

static void wlrOnFailed(void* data, zwlr_screencopy_frame_v1* frame) {
    const auto PSESSION = (CScreencopyPortal::SSession*)data;

    Debug::log(TRACE, "[sc] wlrOnFailed for {}", (void*)PSESSION);

    PSESSION->sharingData.status = FRAME_FAILED;
}

static void wlrOnDamage(void* data, zwlr_screencopy_frame_v1* frame, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    const auto PSESSION = (CScreencopyPortal::SSession*)data;

    Debug::log(TRACE, "[sc] wlrOnDamage for {}", (void*)PSESSION);

    if (PSESSION->sharingData.damageCount > 3) {
        PSESSION->sharingData.damage[0] = {0, 0, PSESSION->sharingData.frameInfoDMA.w, PSESSION->sharingData.frameInfoDMA.h};
        return;
    }

    PSESSION->sharingData.damage[PSESSION->sharingData.damageCount++] = {x, y, width, height};

    Debug::log(TRACE, "[sc] wlr damage: {} {} {} {}", x, y, width, height);
}

static void wlrOnDmabuf(void* data, zwlr_screencopy_frame_v1* frame, uint32_t format, uint32_t width, uint32_t height) {
    const auto PSESSION = (CScreencopyPortal::SSession*)data;

    Debug::log(TRACE, "[sc] wlrOnDmabuf for {}", (void*)PSESSION);

    PSESSION->sharingData.frameInfoDMA.w   = width;
    PSESSION->sharingData.frameInfoDMA.h   = height;
    PSESSION->sharingData.frameInfoDMA.fmt = format;
}

static void wlrOnBufferDone(void* data, zwlr_screencopy_frame_v1* frame) {
    const auto PSESSION = (CScreencopyPortal::SSession*)data;

    Debug::log(TRACE, "[sc] wlrOnBufferDone for {}", (void*)PSESSION);

    const auto PSTREAM = g_pPortalManager->m_sPortals.screencopy->m_pPipewire->streamFromSession(PSESSION);

    if (!PSTREAM) {
        Debug::log(TRACE, "[sc] wlrOnBufferDone: no stream");
        zwlr_screencopy_frame_v1_destroy(frame);
        PSESSION->sharingData.frameCallback = nullptr;
        PSESSION->sharingData.status        = FRAME_NONE;
        return;
    }

    Debug::log(TRACE, "[sc] pw format {} size {}x{}", (int)PSTREAM->pwVideoInfo.format, PSTREAM->pwVideoInfo.size.width, PSTREAM->pwVideoInfo.size.height);
    Debug::log(TRACE, "[sc] wlr format {} size {}x{}", (int)PSESSION->sharingData.frameInfoSHM.fmt, PSESSION->sharingData.frameInfoSHM.w, PSESSION->sharingData.frameInfoSHM.h);
    Debug::log(TRACE, "[sc] wlr format dma {} size {}x{}", (int)PSESSION->sharingData.frameInfoDMA.fmt, PSESSION->sharingData.frameInfoDMA.w, PSESSION->sharingData.frameInfoDMA.h);

    const auto FMT = PSTREAM->isDMA ? PSESSION->sharingData.frameInfoDMA.fmt : PSESSION->sharingData.frameInfoSHM.fmt;
    if ((PSTREAM->pwVideoInfo.format != pwFromDrmFourcc(FMT) && PSTREAM->pwVideoInfo.format != pwStripAlpha(pwFromDrmFourcc(FMT))) ||
        (PSTREAM->pwVideoInfo.size.width != PSESSION->sharingData.frameInfoDMA.w || PSTREAM->pwVideoInfo.size.height != PSESSION->sharingData.frameInfoDMA.h)) {
        Debug::log(LOG, "[sc] Incompatible formats, renegotiate stream");
        PSESSION->sharingData.status = FRAME_RENEG;
        zwlr_screencopy_frame_v1_destroy(frame);
        g_pPortalManager->m_sPortals.screencopy->m_pPipewire->updateStreamParam(PSTREAM);
        g_pPortalManager->m_sPortals.screencopy->queueNextShareFrame(PSESSION);
        PSESSION->sharingData.status = FRAME_NONE;
        return;
    }

    if (!PSTREAM->currentPWBuffer) {
        Debug::log(TRACE, "[sc] wlrOnBufferDone: dequeue, no current buffer");
        g_pPortalManager->m_sPortals.screencopy->m_pPipewire->dequeue(PSESSION);
    }

    if (!PSTREAM->currentPWBuffer) {
        zwlr_screencopy_frame_v1_destroy(frame);
        PSESSION->sharingData.frameCallback = nullptr;
        Debug::log(LOG, "[screencopy/pipewire] Out of buffers");
        PSESSION->sharingData.status = FRAME_NONE;
        return;
    }

    zwlr_screencopy_frame_v1_copy_with_damage(frame, PSTREAM->currentPWBuffer->wlBuffer);

    Debug::log(TRACE, "[sc] wlr frame copied");
}

static const zwlr_screencopy_frame_v1_listener wlrFrameListener = {
    .buffer       = wlrOnBuffer,
    .flags        = wlrOnFlags,
    .ready        = wlrOnReady,
    .failed       = wlrOnFailed,
    .damage       = wlrOnDamage,
    .linux_dmabuf = wlrOnDmabuf,
    .buffer_done  = wlrOnBufferDone,
};

static void hlOnBuffer(void* data, hyprland_toplevel_export_frame_v1* frame, uint32_t format, uint32_t width, uint32_t height, uint32_t stride) {
    const auto PSESSION = (CScreencopyPortal::SSession*)data;

    Debug::log(TRACE, "[sc] hlOnBuffer for {}", (void*)PSESSION);

    PSESSION->sharingData.frameInfoSHM.w      = width;
    PSESSION->sharingData.frameInfoSHM.h      = height;
    PSESSION->sharingData.frameInfoSHM.fmt    = drmFourccFromSHM((wl_shm_format)format);
    PSESSION->sharingData.frameInfoSHM.size   = stride * height;
    PSESSION->sharingData.frameInfoSHM.stride = stride;

    // todo: done if ver < 3
}

static void hlOnFlags(void* data, hyprland_toplevel_export_frame_v1* frame, uint32_t flags) {
    const auto PSESSION = (CScreencopyPortal::SSession*)data;

    Debug::log(TRACE, "[sc] hlOnFlags for {}", (void*)PSESSION);

    // todo: maybe check for y invert?
}

static void hlOnReady(void* data, hyprland_toplevel_export_frame_v1* frame, uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec) {
    const auto PSESSION = (CScreencopyPortal::SSession*)data;

    Debug::log(TRACE, "[sc] hlOnReady for {}", (void*)PSESSION);

    PSESSION->sharingData.status = FRAME_READY;

    PSESSION->sharingData.tvSec         = ((((uint64_t)tv_sec_hi) << 32) + (uint64_t)tv_sec_lo);
    PSESSION->sharingData.tvNsec        = tv_nsec;
    PSESSION->sharingData.tvTimestampNs = PSESSION->sharingData.tvSec * SPA_NSEC_PER_SEC + PSESSION->sharingData.tvNsec;

    Debug::log(TRACE, "[sc] frame timestamp sec: {} nsec: {} combined: {}ns", PSESSION->sharingData.tvSec, PSESSION->sharingData.tvNsec, PSESSION->sharingData.tvTimestampNs);

    g_pPortalManager->m_sPortals.screencopy->m_pPipewire->enqueue(PSESSION);

    g_pPortalManager->m_sPortals.screencopy->queueNextShareFrame(PSESSION);

    hyprland_toplevel_export_frame_v1_destroy(frame);
    PSESSION->sharingData.windowFrameCallback = nullptr;
}

static void hlOnFailed(void* data, hyprland_toplevel_export_frame_v1* frame) {
    const auto PSESSION = (CScreencopyPortal::SSession*)data;

    Debug::log(TRACE, "[sc] hlOnFailed for {}", (void*)PSESSION);

    PSESSION->sharingData.status = FRAME_FAILED;
}

static void hlOnDamage(void* data, hyprland_toplevel_export_frame_v1* frame, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    const auto PSESSION = (CScreencopyPortal::SSession*)data;

    Debug::log(TRACE, "[sc] hlOnDamage for {}", (void*)PSESSION);

    if (PSESSION->sharingData.damageCount > 3) {
        PSESSION->sharingData.damage[0] = {0, 0, PSESSION->sharingData.frameInfoDMA.w, PSESSION->sharingData.frameInfoDMA.h};
        return;
    }

    PSESSION->sharingData.damage[PSESSION->sharingData.damageCount++] = {x, y, width, height};

    Debug::log(TRACE, "[sc] hl damage: {} {} {} {}", x, y, width, height);
}

static void hlOnDmabuf(void* data, hyprland_toplevel_export_frame_v1* frame, uint32_t format, uint32_t width, uint32_t height) {
    const auto PSESSION = (CScreencopyPortal::SSession*)data;

    Debug::log(TRACE, "[sc] hlOnDmabuf for {}", (void*)PSESSION);

    PSESSION->sharingData.frameInfoDMA.w   = width;
    PSESSION->sharingData.frameInfoDMA.h   = height;
    PSESSION->sharingData.frameInfoDMA.fmt = format;
}

static void hlOnBufferDone(void* data, hyprland_toplevel_export_frame_v1* frame) {
    const auto PSESSION = (CScreencopyPortal::SSession*)data;

    Debug::log(TRACE, "[sc] hlOnBufferDone for {}", (void*)PSESSION);

    const auto PSTREAM = g_pPortalManager->m_sPortals.screencopy->m_pPipewire->streamFromSession(PSESSION);

    if (!PSTREAM) {
        Debug::log(TRACE, "[sc] hlOnBufferDone: no stream");
        hyprland_toplevel_export_frame_v1_destroy(frame);
        PSESSION->sharingData.windowFrameCallback = nullptr;
        PSESSION->sharingData.status              = FRAME_NONE;
        return;
    }

    Debug::log(TRACE, "[sc] pw format {} size {}x{}", (int)PSTREAM->pwVideoInfo.format, PSTREAM->pwVideoInfo.size.width, PSTREAM->pwVideoInfo.size.height);
    Debug::log(TRACE, "[sc] hl format {} size {}x{}", (int)PSESSION->sharingData.frameInfoSHM.fmt, PSESSION->sharingData.frameInfoSHM.w, PSESSION->sharingData.frameInfoSHM.h);

    const auto FMT = PSTREAM->isDMA ? PSESSION->sharingData.frameInfoDMA.fmt : PSESSION->sharingData.frameInfoSHM.fmt;
    if ((PSTREAM->pwVideoInfo.format != pwFromDrmFourcc(FMT) && PSTREAM->pwVideoInfo.format != pwStripAlpha(pwFromDrmFourcc(FMT))) ||
        (PSTREAM->pwVideoInfo.size.width != PSESSION->sharingData.frameInfoDMA.w || PSTREAM->pwVideoInfo.size.height != PSESSION->sharingData.frameInfoDMA.h)) {
        Debug::log(LOG, "[sc] Incompatible formats, renegotiate stream");
        PSESSION->sharingData.status = FRAME_RENEG;
        hyprland_toplevel_export_frame_v1_destroy(frame);
        PSESSION->sharingData.windowFrameCallback = nullptr;
        g_pPortalManager->m_sPortals.screencopy->m_pPipewire->updateStreamParam(PSTREAM);
        g_pPortalManager->m_sPortals.screencopy->queueNextShareFrame(PSESSION);
        PSESSION->sharingData.status = FRAME_NONE;
        return;
    }

    if (!PSTREAM->currentPWBuffer) {
        Debug::log(TRACE, "[sc] wlrOnBufferDone: dequeue, no current buffer");
        g_pPortalManager->m_sPortals.screencopy->m_pPipewire->dequeue(PSESSION);
    }

    if (!PSTREAM->currentPWBuffer) {
        hyprland_toplevel_export_frame_v1_destroy(frame);
        PSESSION->sharingData.windowFrameCallback = nullptr;
        Debug::log(LOG, "[screencopy/pipewire] Out of buffers");
        PSESSION->sharingData.status = FRAME_NONE;
        return;
    }

    hyprland_toplevel_export_frame_v1_copy(frame, PSTREAM->currentPWBuffer->wlBuffer, false);

    Debug::log(TRACE, "[sc] wlr frame copied");
}

static const hyprland_toplevel_export_frame_v1_listener hyprlandFrameListener = {
    .buffer       = hlOnBuffer,
    .damage       = hlOnDamage,
    .flags        = hlOnFlags,
    .ready        = hlOnReady,
    .failed       = hlOnFailed,
    .linux_dmabuf = hlOnDmabuf,
    .buffer_done  = hlOnBufferDone,
};

// --------------------------------------------------------- //

void CScreencopyPortal::onCreateSession(sdbus::MethodCall& call) {
    sdbus::ObjectPath requestHandle, sessionHandle;

    g_pPortalManager->m_sHelpers.toplevel->activate();

    call >> requestHandle;
    call >> sessionHandle;

    std::string appID;
    call >> appID;

    Debug::log(LOG, "[screencopy] New session:");
    Debug::log(LOG, "[screencopy]  | {}", requestHandle.c_str());
    Debug::log(LOG, "[screencopy]  | {}", sessionHandle.c_str());
    Debug::log(LOG, "[screencopy]  | appid: {}", appID);

    const auto PSESSION = m_vSessions.emplace_back(std::make_unique<SSession>(appID, requestHandle, sessionHandle)).get();

    // create objects
    PSESSION->session            = createDBusSession(sessionHandle);
    PSESSION->session->onDestroy = [PSESSION, this]() {
        if (PSESSION->sharingData.active) {
            m_pPipewire->destroyStream(PSESSION);
            Debug::log(LOG, "[screencopy] Stream destroyed");
        }
        PSESSION->session.release();
        Debug::log(LOG, "[screencopy] Session destroyed");

        // deactivate toplevel so it doesn't listen and waste battery
        g_pPortalManager->m_sHelpers.toplevel->deactivate();
    };
    PSESSION->request            = createDBusRequest(requestHandle);
    PSESSION->request->onDestroy = [PSESSION]() { PSESSION->request.release(); };

    auto reply = call.createReply();
    reply << (uint32_t)0;
    reply << std::unordered_map<std::string, sdbus::Variant>{};
    reply.send();
}

void CScreencopyPortal::onSelectSources(sdbus::MethodCall& call) {
    sdbus::ObjectPath requestHandle, sessionHandle;

    call >> requestHandle;
    call >> sessionHandle;

    std::string appID;
    call >> appID;

    Debug::log(LOG, "[screencopy] SelectSources:");
    Debug::log(LOG, "[screencopy]  | {}", requestHandle.c_str());
    Debug::log(LOG, "[screencopy]  | {}", sessionHandle.c_str());
    Debug::log(LOG, "[screencopy]  | appid: {}", appID);

    const auto PSESSION = getSession(sessionHandle);

    if (!PSESSION) {
        Debug::log(ERR, "[screencopy] SelectSources: no session found??");
        auto reply = call.createErrorReply(sdbus::Error{"NOSESSION", "No session found"});
        reply << (uint32_t)1;
        reply.send();
        return;
    }

    std::unordered_map<std::string, sdbus::Variant> options;

    call >> options;

    struct {
        bool        exists = false;
        std::string token, output;
        uint64_t    windowHandle;
        bool        withCursor;
        uint64_t    timeIssued;
    } restoreData;

    for (auto& [key, val] : options) {

        if (key == "cursor_mode") {
            PSESSION->cursorMode = val.get<uint32_t>();
            Debug::log(LOG, "[screencopy] option cursor_mode to {}", PSESSION->cursorMode);
        } else if (key == "restore_data") {
            // suv
            // v -> r(susbt) -> v2
            // v -> a(sv) -> v3
            std::string issuer;
            uint32_t    version;
            auto        suv = val.get<sdbus::Struct<std::string, uint32_t, sdbus::Variant>>();
            issuer          = suv.get<0>();
            version         = suv.get<1>();

            sdbus::Variant data = suv.get<2>();

            if (issuer != "hyprland") {
                Debug::log(LOG, "[screencopy] Restore token from {}, ignoring", issuer);
                continue;
            }

            Debug::log(LOG, "[screencopy] Restore token from {} ver {}", issuer, version);

            if (version != 2 && version != 3) {
                Debug::log(LOG, "[screencopy] Restore token ver unsupported, skipping", issuer);
                continue;
            }

            if (version == 2) {
                auto susbt = data.get<sdbus::Struct<std::string, uint32_t, std::string, bool, uint64_t>>();

                restoreData.exists = true;

                restoreData.token        = susbt.get<0>();
                restoreData.windowHandle = susbt.get<1>();
                restoreData.output       = susbt.get<2>();
                restoreData.withCursor   = susbt.get<3>();
                restoreData.timeIssued   = susbt.get<4>();

                Debug::log(LOG, "[screencopy] Restore token v2 {} with data: {} {} {} {}", restoreData.token, restoreData.windowHandle, restoreData.output, restoreData.withCursor,
                           restoreData.timeIssued);
            } else {
                // ver 3
                auto        sv = data.get<std::unordered_map<std::string, sdbus::Variant>>();

                uint64_t    windowHandle = 0;
                std::string windowClass;

                restoreData.windowHandle = 0;
                restoreData.exists       = true;

                for (auto& [tkkey, tkval] : sv) {
                    if (tkkey == "output") {
                        restoreData.output = tkval.get<std::string>();
                    } else if (tkkey == "windowHandle") {
                        windowHandle = tkval.get<uint64_t>();
                    } else if (tkkey == "windowClass") {
                        windowClass = tkval.get<std::string>();
                    } else if (tkkey == "withCursor") {
                        restoreData.withCursor = (bool)tkval.get<uint32_t>();
                    } else if (tkkey == "timeIssued") {
                        restoreData.timeIssued = tkval.get<uint64_t>();
                    } else if (tkkey == "token") {
                        restoreData.token = tkval.get<std::string>();
                    } else {
                        Debug::log(LOG, "[screencopy] restore token v3, unknown prop {}", tkkey);
                    }
                }

                Debug::log(LOG, "[screencopy] Restore token v3 {} with data: {} {} {} {} {}", restoreData.token, windowHandle, windowClass, restoreData.output,
                           restoreData.withCursor, restoreData.timeIssued);

                // find window
                if (windowHandle != 0 || !windowClass.empty()) {
                    if (windowHandle != 0) {
                        for (auto& h : g_pPortalManager->m_sHelpers.toplevel->m_vToplevels) {
                            if ((uint64_t)h->handle == windowHandle) {
                                restoreData.windowHandle = (uint64_t)h->handle;
                                Debug::log(LOG, "[screencopy] token v3 window found by handle {}", (void*)windowHandle);
                                break;
                            }
                        }
                    }

                    if (restoreData.windowHandle == 0 && !windowClass.empty()) {
                        // try class
                        for (auto& h : g_pPortalManager->m_sHelpers.toplevel->m_vToplevels) {
                            if (h->windowClass == windowClass) {
                                restoreData.windowHandle = (uint64_t)h->handle;
                                Debug::log(LOG, "[screencopy] token v3 window found by class {}", windowClass);
                                break;
                            }
                        }
                    }
                }
            }

        } else if (key == "persist_mode") {
            PSESSION->persistMode = val.get<uint32_t>();
            Debug::log(LOG, "[screencopy] option persist_mode to {}", PSESSION->persistMode);
        } else {
            Debug::log(LOG, "[screencopy] unused option {}", key);
        }
    }

    const bool RESTOREDATAVALID = restoreData.exists &&
        (g_pPortalManager->m_sHelpers.toplevel->exists((zwlr_foreign_toplevel_handle_v1*)restoreData.windowHandle) || g_pPortalManager->getOutputFromName(restoreData.output));

    SSelectionData SHAREDATA;
    if (RESTOREDATAVALID) {
        Debug::log(LOG, "[screencopy] restore data valid, not prompting");

        SHAREDATA.output       = restoreData.output;
        SHAREDATA.windowHandle = (zwlr_foreign_toplevel_handle_v1*)restoreData.windowHandle;
        SHAREDATA.type         = restoreData.windowHandle ? TYPE_WINDOW : TYPE_OUTPUT;
        PSESSION->cursorMode   = restoreData.withCursor;
    } else {
        Debug::log(LOG, "[screencopy] restore data invalid / missing, prompting");

        SHAREDATA = promptForScreencopySelection();
    }

    Debug::log(LOG, "[screencopy] SHAREDATA returned selection {}", (int)SHAREDATA.type);

    if (SHAREDATA.type == TYPE_WINDOW && !m_sState.toplevel) {
        Debug::log(ERR, "[screencopy] Requested type window for no toplevel export protocol!");
        SHAREDATA.type = TYPE_INVALID;
    }

    PSESSION->selection = SHAREDATA;

    auto reply = call.createReply();
    reply << (uint32_t)(SHAREDATA.type == TYPE_INVALID ? 1 : 0);
    reply << std::unordered_map<std::string, sdbus::Variant>{};
    reply.send();
}

void CScreencopyPortal::onStart(sdbus::MethodCall& call) {
    sdbus::ObjectPath requestHandle, sessionHandle;

    call >> requestHandle;
    call >> sessionHandle;

    std::string appID, parentWindow;
    call >> appID;
    call >> parentWindow;

    Debug::log(LOG, "[screencopy] Start:");
    Debug::log(LOG, "[screencopy]  | {}", requestHandle.c_str());
    Debug::log(LOG, "[screencopy]  | {}", sessionHandle.c_str());
    Debug::log(LOG, "[screencopy]  | appid: {}", appID);
    Debug::log(LOG, "[screencopy]  | parent_window: {}", parentWindow);

    const auto PSESSION = getSession(sessionHandle);

    if (!PSESSION) {
        Debug::log(ERR, "[screencopy] Start: no session found??");
        auto reply = call.createErrorReply(sdbus::Error{"NOSESSION", "No session found"});
        reply << (uint32_t)1;
        reply.send();
        return;
    }

    startSharing(PSESSION);

    auto reply = call.createReply();
    reply << (uint32_t)0;

    std::unordered_map<std::string, sdbus::Variant> options;

    if (PSESSION->persistMode != 0 && PSESSION->selection.allowToken) {
        // give them a token :)
        std::unordered_map<std::string, sdbus::Variant> mapData;

        switch (PSESSION->selection.type) {
            case TYPE_GEOMETRY:
            case TYPE_OUTPUT: mapData["output"] = PSESSION->selection.output; break;
            case TYPE_WINDOW:
                mapData["windowHandle"] = (uint64_t)PSESSION->selection.windowHandle;
                for (auto& w : g_pPortalManager->m_sHelpers.toplevel->m_vToplevels) {
                    if (w->handle == PSESSION->selection.windowHandle) {
                        mapData["windowClass"] = w->windowClass;
                        break;
                    }
                }
                break;
            default: Debug::log(ERR, "[screencopy] wonk selection in token saving"); break;
        }
        mapData["timeIssued"] = uint64_t(time(nullptr));
        mapData["token"]      = std::string("todo");
        mapData["withCursor"] = PSESSION->cursorMode;

        sdbus::Variant                                       restoreData{mapData};
        sdbus::Struct<std::string, uint32_t, sdbus::Variant> fullRestoreStruct{"hyprland", 3, restoreData};
        options["restore_data"] = sdbus::Variant{fullRestoreStruct};

        Debug::log(LOG, "[screencopy] Sent restore token to {}", PSESSION->sessionHandle.c_str());
    }

    uint32_t type = 0;
    switch (PSESSION->selection.type) {
        case TYPE_OUTPUT: type = 1 << MONITOR; break;
        case TYPE_WINDOW: type = 1 << WINDOW; break;
        case TYPE_GEOMETRY:
        case TYPE_WORKSPACE: type = 1 << VIRTUAL; break;
        default: type = 0; break;
    }
    options["source_type"] = type;

    std::vector<sdbus::Struct<uint32_t, std::unordered_map<std::string, sdbus::Variant>>> streams;

    std::unordered_map<std::string, sdbus::Variant>                                       streamData;
    streamData["position"]    = sdbus::Variant{sdbus::Struct<int32_t, int32_t>{0, 0}};
    streamData["size"]        = sdbus::Variant{sdbus::Struct<int32_t, int32_t>{PSESSION->sharingData.frameInfoSHM.w, PSESSION->sharingData.frameInfoSHM.h}};
    streamData["source_type"] = sdbus::Variant{uint32_t{type}};
    streams.emplace_back(sdbus::Struct<uint32_t, std::unordered_map<std::string, sdbus::Variant>>{PSESSION->sharingData.nodeID, streamData});

    options["streams"] = streams;

    reply << options;

    reply.send();
}

void CScreencopyPortal::startSharing(CScreencopyPortal::SSession* pSession) {
    pSession->sharingData.active = true;

    startFrameCopy(pSession);

    wl_display_dispatch(g_pPortalManager->m_sWaylandConnection.display);
    wl_display_roundtrip(g_pPortalManager->m_sWaylandConnection.display);

    if (pSession->sharingData.frameInfoDMA.fmt == DRM_FORMAT_INVALID) {
        Debug::log(ERR, "[screencopy] Couldn't obtain a format from dma"); // todo: blocks shm
        return;
    }

    m_pPipewire->createStream(pSession);

    while (pSession->sharingData.nodeID == SPA_ID_INVALID) {
        int ret = pw_loop_iterate(g_pPortalManager->m_sPipewire.loop, 0);
        if (ret < 0) {
            Debug::log(ERR, "[pipewire] pw_loop_iterate failed with {}", spa_strerror(ret));
            return;
        }
    }

    Debug::log(LOG, "[screencopy] Sharing initialized");

    g_pPortalManager->m_sPortals.screencopy->queueNextShareFrame(pSession);

    Debug::log(TRACE, "[sc] queued frame in {}ms", 1000.0 / pSession->sharingData.framerate);
}

void CScreencopyPortal::startFrameCopy(CScreencopyPortal::SSession* pSession) {
    const auto POUTPUT = g_pPortalManager->getOutputFromName(pSession->selection.output);

    if (!pSession->sharingData.active) {
        Debug::log(TRACE, "[sc] startFrameCopy: not copying, inactive session");
        return;
    }

    if (!POUTPUT && (pSession->selection.type == TYPE_GEOMETRY || pSession->selection.type == TYPE_OUTPUT)) {
        Debug::log(ERR, "[screencopy] Output {} not found??", pSession->selection.output);
        return;
    }

    if ((pSession->sharingData.frameCallback && (pSession->selection.type == TYPE_GEOMETRY || pSession->selection.type == TYPE_OUTPUT)) ||
        (pSession->sharingData.windowFrameCallback && pSession->selection.type == TYPE_WINDOW)) {
        Debug::log(ERR, "[screencopy] tried scheduling on already scheduled cb (type {})", (int)pSession->selection.type);
        return;
    }

    if (pSession->selection.type == TYPE_GEOMETRY) {
        pSession->sharingData.frameCallback = zwlr_screencopy_manager_v1_capture_output_region(m_sState.screencopy, pSession->cursorMode, POUTPUT->output, pSession->selection.x,
                                                                                               pSession->selection.y, pSession->selection.w, pSession->selection.h);
        pSession->sharingData.transform     = POUTPUT->transform;
    } else if (pSession->selection.type == TYPE_OUTPUT) {
        pSession->sharingData.frameCallback = zwlr_screencopy_manager_v1_capture_output(m_sState.screencopy, pSession->cursorMode, POUTPUT->output);
        pSession->sharingData.transform     = POUTPUT->transform;
    } else if (pSession->selection.type == TYPE_WINDOW) {
        if (!pSession->selection.windowHandle) {
            Debug::log(ERR, "[screencopy] selected invalid window?");
            return;
        }
        pSession->sharingData.windowFrameCallback =
            hyprland_toplevel_export_manager_v1_capture_toplevel_with_wlr_toplevel_handle(m_sState.toplevel, pSession->cursorMode, pSession->selection.windowHandle);
        pSession->sharingData.transform = WL_OUTPUT_TRANSFORM_NORMAL;
    } else {
        Debug::log(ERR, "[screencopy] Unsupported selection {}", (int)pSession->selection.type);
        return;
    }

    pSession->sharingData.status = FRAME_QUEUED;

    if (pSession->sharingData.frameCallback)
        zwlr_screencopy_frame_v1_add_listener(pSession->sharingData.frameCallback, &wlrFrameListener, pSession);
    else if (pSession->sharingData.windowFrameCallback)
        hyprland_toplevel_export_frame_v1_add_listener(pSession->sharingData.windowFrameCallback, &hyprlandFrameListener, pSession);

    Debug::log(TRACE, "[screencopy] frame callbacks initialized");
}

void CScreencopyPortal::queueNextShareFrame(CScreencopyPortal::SSession* pSession) {
    const auto PSTREAM = m_pPipewire->streamFromSession(pSession);

    if (PSTREAM && !PSTREAM->streamState)
        return;

    g_pPortalManager->addTimer({1000.0 / pSession->sharingData.framerate, [pSession]() { g_pPortalManager->m_sPortals.screencopy->startFrameCopy(pSession); }});
}
bool CScreencopyPortal::hasToplevelCapabilities() {
    return m_sState.toplevel;
}

CScreencopyPortal::SSession* CScreencopyPortal::getSession(sdbus::ObjectPath& path) {
    for (auto& s : m_vSessions) {
        if (s->sessionHandle == path)
            return s.get();
    }

    return nullptr;
}

CScreencopyPortal::CScreencopyPortal(zwlr_screencopy_manager_v1* mgr) {
    m_pObject = sdbus::createObject(*g_pPortalManager->getConnection(), OBJECT_PATH);

    m_pObject->registerMethod(INTERFACE_NAME, "CreateSession", "oosa{sv}", "ua{sv}", [&](sdbus::MethodCall c) { onCreateSession(c); });
    m_pObject->registerMethod(INTERFACE_NAME, "SelectSources", "oosa{sv}", "ua{sv}", [&](sdbus::MethodCall c) { onSelectSources(c); });
    m_pObject->registerMethod(INTERFACE_NAME, "Start", "oossa{sv}", "ua{sv}", [&](sdbus::MethodCall c) { onStart(c); });
    m_pObject->registerProperty(INTERFACE_NAME, "AvailableSourceTypes", "u", [](sdbus::PropertyGetReply& reply) -> void { reply << (uint32_t)(VIRTUAL | MONITOR | WINDOW); });
    m_pObject->registerProperty(INTERFACE_NAME, "AvailableCursorModes", "u", [](sdbus::PropertyGetReply& reply) -> void { reply << (uint32_t)(HIDDEN | EMBEDDED); });
    m_pObject->registerProperty(INTERFACE_NAME, "version", "u", [](sdbus::PropertyGetReply& reply) -> void { reply << (uint32_t)(3); });

    m_pObject->finishRegistration();

    m_sState.screencopy = mgr;
    m_pPipewire         = std::make_unique<CPipewireConnection>();

    Debug::log(LOG, "[screencopy] init successful");
}

void CScreencopyPortal::appendToplevelExport(void* proto) {
    m_sState.toplevel = (hyprland_toplevel_export_manager_v1*)proto;

    Debug::log(LOG, "[screencopy] Registered for toplevel export");
}

bool CPipewireConnection::good() {
    return m_pContext && m_pCore;
}

CPipewireConnection::CPipewireConnection() {
    m_pContext = pw_context_new(g_pPortalManager->m_sPipewire.loop, nullptr, 0);

    if (!m_pContext) {
        Debug::log(ERR, "[pipewire] pw didn't allow for a context");
        return;
    }

    m_pCore = pw_context_connect(m_pContext, nullptr, 0);

    if (!m_pCore) {
        Debug::log(ERR, "[pipewire] pw didn't allow for a context connection");
        return;
    }

    Debug::log(LOG, "[pipewire] connected");
}

CPipewireConnection::~CPipewireConnection() {
    if (m_pCore)
        pw_core_disconnect(m_pCore);
    if (m_pContext)
        pw_context_destroy(m_pContext);
}

// --------------- Pipewire Stream Handlers --------------- //

static void pwStreamStateChange(void* data, pw_stream_state old, pw_stream_state state, const char* error) {
    const auto PSTREAM = (CPipewireConnection::SPWStream*)data;

    PSTREAM->pSession->sharingData.nodeID = pw_stream_get_node_id(PSTREAM->stream);

    Debug::log(TRACE, "[pw] pwStreamStateChange on {} from {} to {}, node id {}", (void*)PSTREAM, pw_stream_state_as_string(old), pw_stream_state_as_string(state),
               PSTREAM->pSession->sharingData.nodeID);

    switch (state) {
        case PW_STREAM_STATE_STREAMING:
            PSTREAM->streamState = true;
            if (PSTREAM->pSession->sharingData.status == FRAME_NONE)
                g_pPortalManager->m_sPortals.screencopy->startFrameCopy(PSTREAM->pSession);
            break;
        default: PSTREAM->streamState = false; break;
    }

    if (state == PW_STREAM_STATE_UNCONNECTED)
        g_pPortalManager->m_sPortals.screencopy->m_pPipewire->destroyStream(PSTREAM->pSession);
}

static void pwStreamParamChanged(void* data, uint32_t id, const spa_pod* param) {
    const auto PSTREAM = (CPipewireConnection::SPWStream*)data;

    Debug::log(TRACE, "[pw] pwStreamParamChanged on {}", (void*)PSTREAM);

    if (id != SPA_PARAM_Format || !param) {
        Debug::log(TRACE, "[pw] invalid call in pwStreamParamChanged");
        return;
    }

    spa_pod_dynamic_builder dynBuilder[3];
    const spa_pod*          params[4];
    uint8_t                 params_buffer[3][1024];

    spa_pod_dynamic_builder_init(&dynBuilder[0], params_buffer[0], sizeof(params_buffer[0]), 2048);
    spa_pod_dynamic_builder_init(&dynBuilder[1], params_buffer[1], sizeof(params_buffer[1]), 2048);
    spa_pod_dynamic_builder_init(&dynBuilder[2], params_buffer[2], sizeof(params_buffer[2]), 2048);

    spa_format_video_raw_parse(param, &PSTREAM->pwVideoInfo);
    PSTREAM->pSession->sharingData.framerate = PSTREAM->pwVideoInfo.max_framerate.num / PSTREAM->pwVideoInfo.max_framerate.denom;

    uint32_t                   data_type = 1 << SPA_DATA_MemFd;

    const struct spa_pod_prop* prop_modifier;
    if ((prop_modifier = spa_pod_find_prop(param, nullptr, SPA_FORMAT_VIDEO_modifier))) {
        Debug::log(TRACE, "[pipewire] pw requested dmabuf");
        PSTREAM->isDMA = true;
        data_type      = 1 << SPA_DATA_DmaBuf;

        RASSERT(PSTREAM->pwVideoInfo.format == pwFromDrmFourcc(PSTREAM->pSession->sharingData.frameInfoDMA.fmt), "invalid format in dma pw param change");

        if ((prop_modifier->flags & SPA_POD_PROP_FLAG_DONT_FIXATE) > 0) {
            Debug::log(TRACE, "[pw] don't fixate");
            const spa_pod* pod_modifier = &prop_modifier->value;

            uint32_t       n_modifiers = SPA_POD_CHOICE_N_VALUES(pod_modifier) - 1;
            uint64_t*      modifiers   = (uint64_t*)SPA_POD_CHOICE_VALUES(pod_modifier);
            modifiers++;
            uint32_t         flags = GBM_BO_USE_RENDERING;
            uint64_t         modifier;
            uint32_t         n_params;
            spa_pod_builder* builder[2] = {&dynBuilder[0].b, &dynBuilder[1].b};

            gbm_bo*          bo =
                gbm_bo_create_with_modifiers2(g_pPortalManager->m_sWaylandConnection.gbmDevice, PSTREAM->pSession->sharingData.frameInfoDMA.w,
                                              PSTREAM->pSession->sharingData.frameInfoDMA.h, PSTREAM->pSession->sharingData.frameInfoDMA.fmt, modifiers, n_modifiers, flags);
            if (bo) {
                modifier = gbm_bo_get_modifier(bo);
                gbm_bo_destroy(bo);
                goto fixate_format;
            }

            Debug::log(TRACE, "[pw] unable to allocate a dmabuf with modifiers. Falling back to the old api");
            for (uint32_t i = 0; i < n_modifiers; i++) {
                switch (modifiers[i]) {
                    case DRM_FORMAT_MOD_INVALID:
                        flags =
                            GBM_BO_USE_RENDERING; // ;cast->ctx->state->config->screencast_conf.force_mod_linear ? GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR : GBM_BO_USE_RENDERING;
                        break;
                    case DRM_FORMAT_MOD_LINEAR: flags = GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR; break;
                    default: continue;
                }
                bo = gbm_bo_create(g_pPortalManager->m_sWaylandConnection.gbmDevice, PSTREAM->pSession->sharingData.frameInfoDMA.w, PSTREAM->pSession->sharingData.frameInfoDMA.h,
                                   PSTREAM->pSession->sharingData.frameInfoDMA.fmt, flags);
                if (bo) {
                    modifier = gbm_bo_get_modifier(bo);
                    gbm_bo_destroy(bo);
                    goto fixate_format;
                }
            }

            Debug::log(ERR, "[pw] failed to alloc dma");
            return;

        fixate_format:
            params[0] = fixate_format(&dynBuilder[2].b, pwFromDrmFourcc(PSTREAM->pSession->sharingData.frameInfoDMA.fmt), PSTREAM->pSession->sharingData.frameInfoDMA.w,
                                      PSTREAM->pSession->sharingData.frameInfoDMA.h, PSTREAM->pSession->sharingData.framerate, &modifier);

            n_params = g_pPortalManager->m_sPortals.screencopy->m_pPipewire->buildFormatsFor(builder, &params[1], PSTREAM);
            n_params++;

            pw_stream_update_params(PSTREAM->stream, params, n_params);
            spa_pod_dynamic_builder_clean(&dynBuilder[0]);
            spa_pod_dynamic_builder_clean(&dynBuilder[1]);
            spa_pod_dynamic_builder_clean(&dynBuilder[2]);

            Debug::log(TRACE, "[pw] Format fixated:");
            Debug::log(TRACE, "[pw]  | buffer_type {}", "DMA (No fixate)");
            Debug::log(TRACE, "[pw]  | format: {}", (int)PSTREAM->pwVideoInfo.format);
            Debug::log(TRACE, "[pw]  | modifier: {}", PSTREAM->pwVideoInfo.modifier);
            Debug::log(TRACE, "[pw]  | size: {}x{}", PSTREAM->pwVideoInfo.size.width, PSTREAM->pwVideoInfo.size.height);
            Debug::log(TRACE, "[pw]  | framerate {}", PSTREAM->pSession->sharingData.framerate);

            return;
        }
    }

    Debug::log(TRACE, "[pw] Format renegotiated:");
    Debug::log(TRACE, "[pw]  | buffer_type {}", PSTREAM->isDMA ? "DMA" : "SHM");
    Debug::log(TRACE, "[pw]  | format: {}", (int)PSTREAM->pwVideoInfo.format);
    Debug::log(TRACE, "[pw]  | modifier: {}", PSTREAM->pwVideoInfo.modifier);
    Debug::log(TRACE, "[pw]  | size: {}x{}", PSTREAM->pwVideoInfo.size.width, PSTREAM->pwVideoInfo.size.height);
    Debug::log(TRACE, "[pw]  | framerate {}", PSTREAM->pSession->sharingData.framerate);

    uint32_t blocks = 1;

    params[0] = build_buffer(&dynBuilder[0].b, blocks, PSTREAM->pSession->sharingData.frameInfoSHM.size, PSTREAM->pSession->sharingData.frameInfoSHM.stride, data_type);

    params[1] = (const spa_pod*)spa_pod_builder_add_object(&dynBuilder[1].b, SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta, SPA_PARAM_META_type, SPA_POD_Id(SPA_META_Header),
                                                           SPA_PARAM_META_size, SPA_POD_Int(sizeof(struct spa_meta_header)));

    params[2] = (const spa_pod*)spa_pod_builder_add_object(&dynBuilder[1].b, SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta, SPA_PARAM_META_type, SPA_POD_Id(SPA_META_VideoTransform),
                                                           SPA_PARAM_META_size, SPA_POD_Int(sizeof(struct spa_meta_videotransform)));

    params[3] = (const spa_pod*)spa_pod_builder_add_object(
        &dynBuilder[2].b, SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta, SPA_PARAM_META_type, SPA_POD_Id(SPA_META_VideoDamage), SPA_PARAM_META_size,
        SPA_POD_CHOICE_RANGE_Int(sizeof(struct spa_meta_region) * 4, sizeof(struct spa_meta_region) * 1, sizeof(struct spa_meta_region) * 4));

    pw_stream_update_params(PSTREAM->stream, params, 4);
    spa_pod_dynamic_builder_clean(&dynBuilder[0]);
    spa_pod_dynamic_builder_clean(&dynBuilder[1]);
    spa_pod_dynamic_builder_clean(&dynBuilder[2]);
}

static void pwStreamAddBuffer(void* data, pw_buffer* buffer) {
    const auto PSTREAM = (CPipewireConnection::SPWStream*)data;

    Debug::log(TRACE, "[pw] pwStreamAddBuffer with {} on {}", (void*)buffer, (void*)PSTREAM);

    spa_data*     spaData = buffer->buffer->datas;
    spa_data_type type;

    if ((spaData[0].type & (1u << SPA_DATA_MemFd)) > 0) {
        type = SPA_DATA_MemFd;
        Debug::log(WARN, "[pipewire] Asked for a wl_shm buffer which is legacy.");
    } else if ((spaData[0].type & (1u << SPA_DATA_DmaBuf)) > 0) {
        type = SPA_DATA_DmaBuf;
    } else {
        Debug::log(ERR, "[pipewire] wrong format in addbuffer");
        return;
    }

    const auto PBUFFER = PSTREAM->buffers.emplace_back(g_pPortalManager->m_sPortals.screencopy->m_pPipewire->createBuffer(PSTREAM, type == SPA_DATA_DmaBuf)).get();

    PBUFFER->pwBuffer = buffer;
    buffer->user_data = PBUFFER;

    Debug::log(TRACE, "[pw] buffer datas {}", buffer->buffer->n_datas);

    for (uint32_t plane = 0; plane < buffer->buffer->n_datas; plane++) {
        spaData[plane].type          = type;
        spaData[plane].maxsize       = PBUFFER->size[plane];
        spaData[plane].mapoffset     = 0;
        spaData[plane].chunk->size   = PBUFFER->size[plane];
        spaData[plane].chunk->stride = PBUFFER->stride[plane];
        spaData[plane].chunk->offset = PBUFFER->offset[plane];
        spaData[plane].flags         = 0;
        spaData[plane].fd            = PBUFFER->fd[plane];
        spaData[plane].data          = NULL;
        // clients have implemented to check chunk->size if the buffer is valid instead
        // of using the flags. Until they are patched we should use some arbitrary value.
        if (PBUFFER->isDMABUF && spaData[plane].chunk->size == 0) {
            spaData[plane].chunk->size = 9; // This was choosen by a fair d20.
        }
    }
}

static void pwStreamRemoveBuffer(void* data, pw_buffer* buffer) {
    const auto PSTREAM = (CPipewireConnection::SPWStream*)data;
    const auto PBUFFER = (SBuffer*)buffer->user_data;

    Debug::log(TRACE, "[pw] pwStreamRemoveBuffer with {} on {}", (void*)buffer, (void*)PSTREAM);

    if (!PBUFFER)
        return;

    if (PSTREAM->currentPWBuffer == PBUFFER)
        PSTREAM->currentPWBuffer = nullptr;

    if (PBUFFER->isDMABUF)
        gbm_bo_destroy(PBUFFER->bo);

    wl_buffer_destroy(PBUFFER->wlBuffer);
    for (int plane = 0; plane < PBUFFER->planeCount; plane++) {
        close(PBUFFER->fd[plane]);
    }

    for (uint32_t plane = 0; plane < buffer->buffer->n_datas; plane++) {
        buffer->buffer->datas[plane].fd = -1;
    }

    std::erase_if(PSTREAM->buffers, [&](const auto& other) { return other.get() == PBUFFER; });

    buffer->user_data = nullptr;
}

static const pw_stream_events pwStreamEvents = {
    .version       = PW_VERSION_STREAM_EVENTS,
    .state_changed = pwStreamStateChange,
    .param_changed = pwStreamParamChanged,
    .add_buffer    = pwStreamAddBuffer,
    .remove_buffer = pwStreamRemoveBuffer,
};

// ------------------------------------------------------- //

void CPipewireConnection::createStream(CScreencopyPortal::SSession* pSession) {
    const auto PSTREAM = m_vStreams.emplace_back(std::make_unique<SPWStream>(pSession)).get();

    pw_loop_enter(g_pPortalManager->m_sPipewire.loop);

    uint8_t                 buffer[2][1024];
    spa_pod_dynamic_builder dynBuilder[2];
    spa_pod_dynamic_builder_init(&dynBuilder[0], buffer[0], sizeof(buffer[0]), 2048);
    spa_pod_dynamic_builder_init(&dynBuilder[1], buffer[1], sizeof(buffer[1]), 2048);

    const std::string NAME = getRandName("xdph-streaming-");

    PSTREAM->stream = pw_stream_new(m_pCore, NAME.c_str(), pw_properties_new(PW_KEY_MEDIA_CLASS, "Video/Source", nullptr));

    Debug::log(TRACE, "[pw] New stream name {}", NAME);

    if (!PSTREAM->stream) {
        Debug::log(ERR, "[pipewire] refused to create stream");
        g_pPortalManager->terminate();
        return;
    }

    spa_pod_builder* builder[2] = {&dynBuilder[0].b, &dynBuilder[1].b};
    const spa_pod*   params[2];
    const auto       PARAMCOUNT = buildFormatsFor(builder, params, PSTREAM);

    spa_pod_dynamic_builder_clean(&dynBuilder[0]);
    spa_pod_dynamic_builder_clean(&dynBuilder[1]);

    pw_stream_add_listener(PSTREAM->stream, &PSTREAM->streamListener, &pwStreamEvents, PSTREAM);

    pw_stream_connect(PSTREAM->stream, PW_DIRECTION_OUTPUT, PW_ID_ANY, (pw_stream_flags)(PW_STREAM_FLAG_DRIVER | PW_STREAM_FLAG_ALLOC_BUFFERS), params, PARAMCOUNT);

    pSession->sharingData.nodeID = pw_stream_get_node_id(PSTREAM->stream);

    Debug::log(TRACE, "[pw] Stream got nodeid {}", pSession->sharingData.nodeID);
}

void CPipewireConnection::destroyStream(CScreencopyPortal::SSession* pSession) {
    // Disconnecting the stream can cause reentrance to this function.
    if (pSession->sharingData.active == false)
        return;
    pSession->sharingData.active = false;

    const auto PSTREAM = streamFromSession(pSession);

    if (!PSTREAM || !PSTREAM->stream)
        return;

    if (!PSTREAM->buffers.empty()) {
        std::vector<SBuffer*> bufs;

        for (auto& b : PSTREAM->buffers) {
            bufs.push_back(b.get());
        }

        for (auto& b : bufs) {
            pwStreamRemoveBuffer(PSTREAM, b->pwBuffer);
        }
    }

    pw_stream_flush(PSTREAM->stream, false);
    pw_stream_disconnect(PSTREAM->stream);
    pw_stream_destroy(PSTREAM->stream);

    std::erase_if(m_vStreams, [&](const auto& other) { return other.get() == PSTREAM; });
}

static bool wlr_query_dmabuf_modifiers(uint32_t drm_format, uint32_t num_modifiers, uint64_t* modifiers, uint32_t* max_modifiers) {
    if (g_pPortalManager->m_vDMABUFMods.empty())
        return false;

    if (num_modifiers == 0) {
        *max_modifiers = 0;
        for (auto& mod : g_pPortalManager->m_vDMABUFMods) {
            if (mod.fourcc == drm_format &&
                (mod.mod == DRM_FORMAT_MOD_INVALID || gbm_device_get_format_modifier_plane_count(g_pPortalManager->m_sWaylandConnection.gbmDevice, mod.fourcc, mod.mod) > 0))
                (*max_modifiers)++;
        }
        return true;
    }

    size_t i = 0;
    for (const auto& mod : g_pPortalManager->m_vDMABUFMods) {
        if (i >= num_modifiers)
            break;

        if (mod.fourcc == drm_format &&
            (mod.mod == DRM_FORMAT_MOD_INVALID || gbm_device_get_format_modifier_plane_count(g_pPortalManager->m_sWaylandConnection.gbmDevice, mod.fourcc, mod.mod) > 0)) {
            modifiers[i] = mod.mod;
            ++i;
        }
    }

    *max_modifiers = num_modifiers;
    return true;
}

static bool build_modifierlist(CPipewireConnection::SPWStream* stream, uint32_t drm_format, uint64_t** modifiers, uint32_t* modifier_count) {
    if (!wlr_query_dmabuf_modifiers(drm_format, 0, nullptr, modifier_count)) {
        *modifiers      = NULL;
        *modifier_count = 0;
        return false;
    }
    if (*modifier_count == 0) {
        Debug::log(ERR, "[pw] build_modifierlist: no mods");
        *modifiers = NULL;
        return true;
    }
    *modifiers = (uint64_t*)calloc(*modifier_count, sizeof(uint64_t));
    bool ret   = wlr_query_dmabuf_modifiers(drm_format, *modifier_count, *modifiers, modifier_count);
    Debug::log(TRACE, "[pw] build_modifierlist: count {}", *modifier_count);
    return ret;
}

uint32_t CPipewireConnection::buildFormatsFor(spa_pod_builder* b[2], const spa_pod* params[2], CPipewireConnection::SPWStream* stream) {
    uint32_t  paramCount = 0;
    uint32_t  modCount   = 0;
    uint64_t* modifiers  = nullptr;

    if (build_modifierlist(stream, stream->pSession->sharingData.frameInfoDMA.fmt, &modifiers, &modCount) && modCount > 0) {
        Debug::log(LOG, "[pw] Building modifiers for dma");

        paramCount = 2;
        params[0]  = build_format(b[0], pwFromDrmFourcc(stream->pSession->sharingData.frameInfoDMA.fmt), stream->pSession->sharingData.frameInfoDMA.w,
                                  stream->pSession->sharingData.frameInfoDMA.h, stream->pSession->sharingData.framerate, modifiers, modCount);
        assert(params[0] != NULL);
        params[1] = build_format(b[1], pwFromDrmFourcc(stream->pSession->sharingData.frameInfoSHM.fmt), stream->pSession->sharingData.frameInfoSHM.w,
                                 stream->pSession->sharingData.frameInfoSHM.h, stream->pSession->sharingData.framerate, NULL, 0);
        assert(params[1] != NULL);
    } else {
        Debug::log(LOG, "[pw] Building modifiers for shm");

        paramCount = 1;
        params[0]  = build_format(b[0], pwFromDrmFourcc(stream->pSession->sharingData.frameInfoSHM.fmt), stream->pSession->sharingData.frameInfoSHM.w,
                                  stream->pSession->sharingData.frameInfoSHM.h, stream->pSession->sharingData.framerate, NULL, 0);
    }

    if (modifiers)
        free(modifiers);

    return paramCount;
}

bool CPipewireConnection::buildModListFor(CPipewireConnection::SPWStream* stream, uint32_t drmFmt, uint64_t** mods, uint32_t* modCount) {
    return true;
}

CPipewireConnection::SPWStream* CPipewireConnection::streamFromSession(CScreencopyPortal::SSession* pSession) {
    for (auto& s : m_vStreams) {
        if (s->pSession == pSession)
            return s.get();
    }
    return nullptr;
}

void CPipewireConnection::enqueue(CScreencopyPortal::SSession* pSession) {
    const auto PSTREAM = streamFromSession(pSession);

    if (!PSTREAM) {
        Debug::log(ERR, "[pw] Attempted enqueue on invalid session??");
        return;
    }

    Debug::log(TRACE, "[pw] enqueue on {}", (void*)PSTREAM);

    if (!PSTREAM->currentPWBuffer) {
        Debug::log(ERR, "[pipewire] no buffer in enqueue");
        return;
    }

    spa_buffer* spaBuf  = PSTREAM->currentPWBuffer->pwBuffer->buffer;
    const bool  CORRUPT = PSTREAM->pSession->sharingData.status != FRAME_READY;
    if (CORRUPT)
        Debug::log(TRACE, "[pw] buffer corrupt");

    Debug::log(TRACE, "[pw] Enqueue data:");

    spa_meta_header* header = (spa_meta_header*)spa_buffer_find_meta_data(spaBuf, SPA_META_Header, sizeof(*header));
    if (header) {
        header->pts        = PSTREAM->pSession->sharingData.tvTimestampNs;
        header->flags      = CORRUPT ? SPA_META_HEADER_FLAG_CORRUPTED : 0;
        header->seq        = PSTREAM->seq++;
        header->dts_offset = 0;
        Debug::log(TRACE, "[pw]  | seq {}", header->seq);
        Debug::log(TRACE, "[pw]  | pts {}", header->pts);
    }

    spa_meta_videotransform* vt = (spa_meta_videotransform*)spa_buffer_find_meta_data(spaBuf, SPA_META_VideoTransform, sizeof(*vt));
    if (vt) {
        vt->transform = pSession->sharingData.transform;
        Debug::log(TRACE, "[pw]  | meta transform {}", vt->transform);
    }

    spa_meta* damage = spa_buffer_find_meta(spaBuf, SPA_META_VideoDamage);
    if (damage) {
        Debug::log(TRACE, "[pw]  | meta has damage");

        spa_region* damageRegion  = (spa_region*)spa_meta_first(damage);
        uint32_t    damageCounter = 0;
        do {
            if (damageCounter >= pSession->sharingData.damageCount) {
                *damageRegion = SPA_REGION(0, 0, 0, 0);
                Debug::log(TRACE, "[pw]  | end damage @ {}: {} {} {} {}", damageCounter, damageRegion->position.x, damageRegion->position.y, damageRegion->size.width,
                           damageRegion->size.height);
                break;
            }

            *damageRegion = SPA_REGION(pSession->sharingData.damage[damageCounter].x, pSession->sharingData.damage[damageCounter].y, pSession->sharingData.damage[damageCounter].w,
                                       pSession->sharingData.damage[damageCounter].h);
            Debug::log(TRACE, "[pw]  | damage @ {}: {} {} {} {}", damageCounter, damageRegion->position.x, damageRegion->position.y, damageRegion->size.width,
                       damageRegion->size.height);
            damageCounter++;
        } while (spa_meta_check(damageRegion + 1, damage) && damageRegion++);

        if (damageCounter < pSession->sharingData.damageCount) {
            // TODO: merge damage properly
            *damageRegion = SPA_REGION(0, 0, pSession->sharingData.frameInfoDMA.w, pSession->sharingData.frameInfoDMA.h);
            Debug::log(TRACE, "[pw]  | damage overflow, damaged whole");
        }
    }

    spa_data* datas = spaBuf->datas;

    Debug::log(TRACE, "[pw]  | size {}x{}", PSTREAM->pSession->sharingData.frameInfoDMA.w, PSTREAM->pSession->sharingData.frameInfoDMA.h);

    for (uint32_t plane = 0; plane < spaBuf->n_datas; plane++) {
        datas[plane].chunk->flags = CORRUPT ? SPA_CHUNK_FLAG_CORRUPTED : SPA_CHUNK_FLAG_NONE;

        Debug::log(TRACE, "[pw]  | plane {}", plane);
        Debug::log(TRACE, "[pw]     | fd {}", datas[plane].fd);
        Debug::log(TRACE, "[pw]     | maxsize {}", datas[plane].maxsize);
        Debug::log(TRACE, "[pw]     | size {}", datas[plane].chunk->size);
        Debug::log(TRACE, "[pw]     | stride {}", datas[plane].chunk->stride);
        Debug::log(TRACE, "[pw]     | offset {}", datas[plane].chunk->offset);
        Debug::log(TRACE, "[pw]     | flags {}", datas[plane].chunk->flags);
    }

    Debug::log(TRACE, "[pw] --------------------------------- End enqueue");

    pw_stream_queue_buffer(PSTREAM->stream, PSTREAM->currentPWBuffer->pwBuffer);

    PSTREAM->currentPWBuffer = nullptr;
}

void CPipewireConnection::dequeue(CScreencopyPortal::SSession* pSession) {
    const auto PSTREAM = streamFromSession(pSession);

    if (!PSTREAM) {
        Debug::log(ERR, "[pw] Attempted dequeue on invalid session??");
        return;
    }

    Debug::log(TRACE, "[pw] dequeue on {}", (void*)PSTREAM);

    const auto PWBUF = pw_stream_dequeue_buffer(PSTREAM->stream);

    if (!PWBUF) {
        Debug::log(TRACE, "[pw] dequeue failed");
        PSTREAM->currentPWBuffer = nullptr;
        return;
    }

    const auto PBUF = (SBuffer*)PWBUF->user_data;

    PSTREAM->currentPWBuffer = PBUF;
}

std::unique_ptr<SBuffer> CPipewireConnection::createBuffer(CPipewireConnection::SPWStream* pStream, bool dmabuf) {
    std::unique_ptr<SBuffer> pBuffer = std::make_unique<SBuffer>();

    pBuffer->isDMABUF = dmabuf;

    Debug::log(TRACE, "[pw] createBuffer: type {}", dmabuf ? "dma" : "shm");

    if (dmabuf) {
        pBuffer->w   = pStream->pSession->sharingData.frameInfoDMA.w;
        pBuffer->h   = pStream->pSession->sharingData.frameInfoDMA.h;
        pBuffer->fmt = pStream->pSession->sharingData.frameInfoDMA.fmt;

        uint32_t flags = GBM_BO_USE_RENDERING;

        if (pStream->pwVideoInfo.modifier != DRM_FORMAT_MOD_INVALID) {
            uint64_t* mods = (uint64_t*)&pStream->pwVideoInfo.modifier;
            pBuffer->bo    = gbm_bo_create_with_modifiers2(g_pPortalManager->m_sWaylandConnection.gbmDevice, pBuffer->w, pBuffer->h, pBuffer->fmt, mods, 1, flags);
        } else {
            pBuffer->bo = gbm_bo_create(g_pPortalManager->m_sWaylandConnection.gbmDevice, pBuffer->w, pBuffer->h, pBuffer->fmt, flags);
        }

        if (!pBuffer->bo) {
            Debug::log(ERR, "[pw] Couldn't create a drm buffer");
            return nullptr;
        }

        pBuffer->planeCount = gbm_bo_get_plane_count(pBuffer->bo);

        zwp_linux_buffer_params_v1* params = zwp_linux_dmabuf_v1_create_params((zwp_linux_dmabuf_v1*)g_pPortalManager->m_sWaylandConnection.linuxDmabuf);
        if (!params) {
            Debug::log(ERR, "[pw] zwp_linux_dmabuf_v1_create_params failed");
            gbm_bo_destroy(pBuffer->bo);
            return nullptr;
        }

        for (size_t plane = 0; plane < (size_t)pBuffer->planeCount; plane++) {
            pBuffer->size[plane]   = 0;
            pBuffer->stride[plane] = gbm_bo_get_stride_for_plane(pBuffer->bo, plane);
            pBuffer->offset[plane] = gbm_bo_get_offset(pBuffer->bo, plane);
            uint64_t mod           = gbm_bo_get_modifier(pBuffer->bo);
            pBuffer->fd[plane]     = gbm_bo_get_fd_for_plane(pBuffer->bo, plane);

            if (pBuffer->fd[plane] < 0) {
                Debug::log(ERR, "[pw] gbm_bo_get_fd_for_plane failed");
                zwp_linux_buffer_params_v1_destroy(params);
                gbm_bo_destroy(pBuffer->bo);
                for (size_t plane_tmp = 0; plane_tmp < plane; plane_tmp++) {
                    close(pBuffer->fd[plane_tmp]);
                }
                return NULL;
            }

            zwp_linux_buffer_params_v1_add(params, pBuffer->fd[plane], plane, pBuffer->offset[plane], pBuffer->stride[plane], mod >> 32, mod & 0xffffffff);
        }

        pBuffer->wlBuffer = zwp_linux_buffer_params_v1_create_immed(params, pBuffer->w, pBuffer->h, pBuffer->fmt, /* flags */ 0);
        zwp_linux_buffer_params_v1_destroy(params);

        if (!pBuffer->wlBuffer) {
            Debug::log(ERR, "[pw] zwp_linux_buffer_params_v1_create_immed failed");
            gbm_bo_destroy(pBuffer->bo);
            for (size_t plane = 0; plane < (size_t)pBuffer->planeCount; plane++) {
                close(pBuffer->fd[plane]);
            }

            return nullptr;
        }
    } else {

        pBuffer->w   = pStream->pSession->sharingData.frameInfoSHM.w;
        pBuffer->h   = pStream->pSession->sharingData.frameInfoSHM.h;
        pBuffer->fmt = pStream->pSession->sharingData.frameInfoSHM.fmt;

        pBuffer->planeCount = 1;
        pBuffer->size[0]    = pStream->pSession->sharingData.frameInfoSHM.size;
        pBuffer->stride[0]  = pStream->pSession->sharingData.frameInfoSHM.stride;
        pBuffer->offset[0]  = 0;
        pBuffer->fd[0]      = anonymous_shm_open();

        if (pBuffer->fd[0] == -1) {
            Debug::log(ERR, "[screencopy] anonymous_shm_open failed");
            return nullptr;
        }

        if (ftruncate(pBuffer->fd[0], pBuffer->size[0]) < 0) {
            Debug::log(ERR, "[screencopy] ftruncate failed");
            return nullptr;
        }

        pBuffer->wlBuffer = import_wl_shm_buffer(pBuffer->fd[0], wlSHMFromDrmFourcc(pStream->pSession->sharingData.frameInfoSHM.fmt), pStream->pSession->sharingData.frameInfoSHM.w,
                                                 pStream->pSession->sharingData.frameInfoSHM.h, pStream->pSession->sharingData.frameInfoSHM.stride);
        if (!pBuffer->wlBuffer) {
            Debug::log(ERR, "[screencopy] import_wl_shm_buffer failed");
            return nullptr;
        }
    }

    return pBuffer;
}

void CPipewireConnection::updateStreamParam(SPWStream* pStream) {
    Debug::log(TRACE, "[pw] update stream params");

    uint8_t                 paramsBuf[2][1024];
    spa_pod_dynamic_builder dynBuilder[2];
    spa_pod_dynamic_builder_init(&dynBuilder[0], paramsBuf[0], sizeof(paramsBuf[0]), 2048);
    spa_pod_dynamic_builder_init(&dynBuilder[1], paramsBuf[1], sizeof(paramsBuf[1]), 2048);
    const spa_pod*   params[2];

    spa_pod_builder* builder[2] = {&dynBuilder[0].b, &dynBuilder[1].b};
    uint32_t         n_params   = buildFormatsFor(builder, params, pStream);

    pw_stream_update_params(pStream->stream, params, n_params);
    spa_pod_dynamic_builder_clean(&dynBuilder[0]);
    spa_pod_dynamic_builder_clean(&dynBuilder[1]);
}

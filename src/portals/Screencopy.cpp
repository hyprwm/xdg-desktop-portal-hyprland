#include "Screencopy.hpp"
#include "../core/PortalManager.hpp"
#include "../helpers/Log.hpp"
#include "../helpers/MiscFunctions.hpp"

#include <libdrm/drm_fourcc.h>
#include <pipewire/pipewire.h>

// --------------- Wayland Protocol Handlers --------------- //

static void wlrOnBuffer(void* data, struct zwlr_screencopy_frame_v1* frame, uint32_t format, uint32_t width, uint32_t height, uint32_t stride) {
    const auto PSESSION = (CScreencopyPortal::SSession*)data;

    Debug::log(TRACE, "[sc] wlrOnBuffer for {}", (void*)PSESSION);

    PSESSION->sharingData.frameInfoSHM.w      = width;
    PSESSION->sharingData.frameInfoSHM.h      = height;
    PSESSION->sharingData.frameInfoSHM.fmt    = drmFourccFromSHM((wl_shm_format)format);
    PSESSION->sharingData.frameInfoSHM.size   = stride * height;
    PSESSION->sharingData.frameInfoSHM.stride = stride;

    // todo: done if ver < 3
}

static void wlrOnFlags(void* data, struct zwlr_screencopy_frame_v1* frame, uint32_t flags) {
    const auto PSESSION = (CScreencopyPortal::SSession*)data;

    Debug::log(TRACE, "[sc] wlrOnFlags for {}", (void*)PSESSION);

    // todo: maybe check for y invert?
}

static void wlrOnReady(void* data, struct zwlr_screencopy_frame_v1* frame, uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec) {
    const auto PSESSION = (CScreencopyPortal::SSession*)data;

    Debug::log(TRACE, "[sc] wlrOnReady for {}", (void*)PSESSION);

    PSESSION->sharingData.status = FRAME_READY;

    PSESSION->sharingData.tvSec  = ((((uint64_t)tv_sec_hi) << 32) | tv_sec_lo);
    PSESSION->sharingData.tvNsec = tv_nsec;

    g_pPortalManager->m_sPortals.screencopy->m_pPipewire->enqueue(PSESSION);

    g_pPortalManager->m_vTimers.emplace_back(std::make_unique<CTimer>(1000.0 / FRAMERATE, [PSESSION]() { g_pPortalManager->m_sPortals.screencopy->startFrameCopy(PSESSION); }));

    zwlr_screencopy_frame_v1_destroy(frame);
    PSESSION->sharingData.frameCallback = nullptr;
}

static void wlrOnFailed(void* data, struct zwlr_screencopy_frame_v1* frame) {
    const auto PSESSION = (CScreencopyPortal::SSession*)data;

    Debug::log(TRACE, "[sc] wlrOnFailed for {}", (void*)PSESSION);

    PSESSION->sharingData.status = FRAME_FAILED;
}

static void wlrOnDamage(void* data, struct zwlr_screencopy_frame_v1* frame, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    const auto PSESSION = (CScreencopyPortal::SSession*)data;

    Debug::log(TRACE, "[sc] wlrOnDamage for {}", (void*)PSESSION);

    // todo
}

static void wlrOnDmabuf(void* data, struct zwlr_screencopy_frame_v1* frame, uint32_t format, uint32_t width, uint32_t height) {
    const auto PSESSION = (CScreencopyPortal::SSession*)data;

    Debug::log(TRACE, "[sc] wlrOnDmabuf for {}", (void*)PSESSION);

    PSESSION->sharingData.frameInfoDMA.w   = width;
    PSESSION->sharingData.frameInfoDMA.h   = height;
    PSESSION->sharingData.frameInfoDMA.fmt = format;
}

static void wlrOnBufferDone(void* data, struct zwlr_screencopy_frame_v1* frame) {
    const auto PSESSION = (CScreencopyPortal::SSession*)data;

    Debug::log(TRACE, "[sc] wlrOnBufferDone for {}", (void*)PSESSION);

    const auto PSTREAM = g_pPortalManager->m_sPortals.screencopy->m_pPipewire->streamFromSession(PSESSION);

    if (!PSTREAM) {
        Debug::log(TRACE, "[sc] wlrOnBufferDone: no stream");
        zwlr_screencopy_frame_v1_destroy(frame);
        PSESSION->sharingData.frameCallback = nullptr;
        return;
    }

    Debug::log(TRACE, "[sc] pw format {} size {}x{}", (int)PSTREAM->pwVideoInfo.format, PSTREAM->pwVideoInfo.size.width, PSTREAM->pwVideoInfo.size.height);
    Debug::log(TRACE, "[sc] wlr format {} size {}x{}", (int)PSESSION->sharingData.frameInfoSHM.fmt, PSESSION->sharingData.frameInfoSHM.w, PSESSION->sharingData.frameInfoSHM.h);

    if (!PSTREAM->currentPWBuffer) {
        Debug::log(TRACE, "[sc] wlrOnBufferDone: dequeue, no current buffer");
        g_pPortalManager->m_sPortals.screencopy->m_pPipewire->dequeue(PSESSION);
    }

    if (!PSTREAM->currentPWBuffer) {
        zwlr_screencopy_frame_v1_destroy(frame);
        PSESSION->sharingData.frameCallback = nullptr;
        Debug::log(LOG, "[screencopy/pipewire] Out of buffers");
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

// --------------------------------------------------------- //

void onCloseRequest(sdbus::MethodCall& call, CScreencopyPortal::SSession* sess) {
    if (!sess || !sess->request)
        return;

    auto r = call.createReply();
    r.send();

    sess->request.release();
}

void onCloseSession(sdbus::MethodCall& call, CScreencopyPortal::SSession* sess) {
    if (!sess || !sess->session)
        return;

    auto r = call.createReply();
    r.send();

    sess->session.release();
}

void CScreencopyPortal::onCreateSession(sdbus::MethodCall& call) {
    sdbus::ObjectPath requestHandle, sessionHandle;

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
    PSESSION->request = sdbus::createObject(*g_pPortalManager->getConnection(), requestHandle);
    PSESSION->session = sdbus::createObject(*g_pPortalManager->getConnection(), sessionHandle);

    PSESSION->request->registerMethod("org.freedesktop.impl.portal.Request", "Close", "", "", [&](sdbus::MethodCall c) { onCloseRequest(c, PSESSION); });
    PSESSION->session->registerMethod("org.freedesktop.impl.portal.Session", "Close", "", "", [&](sdbus::MethodCall c) { onCloseSession(c, PSESSION); });

    PSESSION->request->finishRegistration();
    PSESSION->session->finishRegistration();

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
        uint32_t    windowHandle;
        bool        withCursor;
        uint64_t    timeIssued;
    } restoreData;

    for (auto& [key, val] : options) {

        if (key == "cursor_mode") {
            PSESSION->cursorMode = val.get<uint32_t>();
            Debug::log(LOG, "[screencopy] option cursor_mode to {}", PSESSION->cursorMode);
        } else if (key == "restore_data") {
            // suv
            // v -> r(susbt)
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

            if (version != 2) {
                Debug::log(LOG, "[screencopy] Restore token ver unsupported, skipping", issuer);
                continue;
            }

            auto susbt = data.get<sdbus::Struct<std::string, uint32_t, std::string, bool, uint64_t>>();

            restoreData.exists = true;

            restoreData.token        = susbt.get<0>();
            restoreData.windowHandle = susbt.get<1>();
            restoreData.output       = susbt.get<2>();
            restoreData.withCursor   = susbt.get<3>();
            restoreData.timeIssued   = susbt.get<4>();

            Debug::log(LOG, "[screencopy] Restore token {} with data: {} {} {} {}", restoreData.token, restoreData.windowHandle, restoreData.output, restoreData.withCursor,
                       restoreData.timeIssued);
        } else if (key == "persist_mode") {
            PSESSION->persistMode = val.get<uint32_t>();
            Debug::log(LOG, "[screencopy] option persist_mode to {}", PSESSION->persistMode);
        } else {
            Debug::log(LOG, "[screencopy] unused option {}", key);
        }
    }

    auto SHAREDATA = promptForScreencopySelection();

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

    if (PSESSION->persistMode != 0) {
        // give them a token :)
        sdbus::Struct<std::string, uint32_t, std::string, bool, uint64_t> structData{"todo", (uint32_t)PSESSION->selection.windowHandle, PSESSION->selection.output,
                                                                                     (bool)PSESSION->cursorMode, (uint64_t)time(NULL)};
        sdbus::Variant                                                    restoreData{structData};
        sdbus::Struct<std::string, uint32_t, sdbus::Variant>              fullRestoreStruct{"hyprland", 2, restoreData};
        options["restore_data"] = sdbus::Variant{fullRestoreStruct};
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

    if (pSession->sharingData.frameInfoSHM.fmt == DRM_FORMAT_INVALID) {
        Debug::log(ERR, "[screencopy] Couldn't obtain a format from shm");
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

    g_pPortalManager->m_vTimers.emplace_back(std::make_unique<CTimer>(1000.0 / FRAMERATE, [pSession]() { g_pPortalManager->m_sPortals.screencopy->startFrameCopy(pSession); }));

    Debug::log(TRACE, "[sc] queued frame in {}ms", 1000.0 / FRAMERATE);
}

void CScreencopyPortal::startFrameCopy(CScreencopyPortal::SSession* pSession) {
    const auto POUTPUT = g_pPortalManager->getOutputFromName(pSession->selection.output);

    if (!pSession->sharingData.active) {
        Debug::log(TRACE, "[sc] startFrameCopy: not copying, inactive session");
        return;
    }

    if (!POUTPUT) {
        Debug::log(ERR, "[screencopy] Output {} not found??", pSession->selection.output);
        return;
    }

    if (pSession->sharingData.frameCallback) {
        Debug::log(ERR, "[screencopy] tried scheduling on already scheduled cb");
        return;
    }

    if (pSession->selection.type == TYPE_GEOMETRY)
        pSession->sharingData.frameCallback = zwlr_screencopy_manager_v1_capture_output_region(m_sState.screencopy, pSession->cursorMode, POUTPUT->output, pSession->selection.x,
                                                                                               pSession->selection.y, pSession->selection.w, pSession->selection.h);
    else if (pSession->selection.type == TYPE_OUTPUT)
        pSession->sharingData.frameCallback = zwlr_screencopy_manager_v1_capture_output(m_sState.screencopy, pSession->cursorMode, POUTPUT->output);
    else {
        Debug::log(ERR, "[screencopy] Unsupported selection {}", (int)pSession->selection.type);
        return;
    }

    pSession->sharingData.status = FRAME_QUEUED;

    zwlr_screencopy_frame_v1_add_listener(pSession->sharingData.frameCallback, &wlrFrameListener, pSession);

    Debug::log(LOG, "[screencopy] frame callbacks initialized");
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
        case PW_STREAM_STATE_PAUSED:
            if (old == PW_STREAM_STATE_STREAMING)
                g_pPortalManager->m_sPortals.screencopy->m_pPipewire->enqueue(PSTREAM->pSession);
            PSTREAM->streamState = false;
            break;
        default: PSTREAM->streamState = false;
    }

    if (state == PW_STREAM_STATE_UNCONNECTED) {
        g_pPortalManager->m_sPortals.screencopy->m_pPipewire->destroyStream(PSTREAM->pSession);
    }
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
    // todo: framerate

    const struct spa_pod_prop* prop_modifier;
    if ((prop_modifier = spa_pod_find_prop(param, NULL, SPA_FORMAT_VIDEO_modifier)) != NULL) {
        Debug::log(ERR, "[pipewire] pw requested dmabuf");
        return;
    }

    Debug::log(TRACE, "[pw] Format renegotiated:");
    Debug::log(TRACE, "[pw]  | buffer_type {}", "SHM");
    Debug::log(TRACE, "[pw]  | format: {}", (int)PSTREAM->pwVideoInfo.format);
    Debug::log(TRACE, "[pw]  | modifier: {}", PSTREAM->pwVideoInfo.modifier);
    Debug::log(TRACE, "[pw]  | size: {}x{}", PSTREAM->pwVideoInfo.size.width, PSTREAM->pwVideoInfo.size.height);
    Debug::log(TRACE, "[pw]  | framerate {}", FRAMERATE);

    uint32_t blocks    = 1;
    uint32_t data_type = 1 << SPA_DATA_MemFd;

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
    } else if ((spaData[0].type & (1u << SPA_DATA_DmaBuf)) > 0) {
        type = SPA_DATA_DmaBuf;
    } else {
        Debug::log(ERR, "[pipewire] wrong format in addbuffer");
        return;
    }

    const auto PBUFFER = PSTREAM->buffers.emplace_back(std::make_unique<SBuffer>()).get();

    buffer->user_data = PBUFFER;

    PBUFFER->fmt      = PSTREAM->pSession->sharingData.frameInfoSHM.fmt;
    PBUFFER->h        = PSTREAM->pSession->sharingData.frameInfoSHM.h;
    PBUFFER->w        = PSTREAM->pSession->sharingData.frameInfoSHM.w;
    PBUFFER->isDMABUF = type == SPA_DATA_DmaBuf;
    PBUFFER->pwBuffer = buffer;

    Debug::log(TRACE, "[pw] Adding buffer of type {}", PBUFFER->isDMABUF ? "DMA" : "SHM");

    // wl_shm only
    PBUFFER->planeCount = 1;
    PBUFFER->size[0]    = PSTREAM->pSession->sharingData.frameInfoSHM.size;
    PBUFFER->stride[0]  = PSTREAM->pSession->sharingData.frameInfoSHM.stride;
    PBUFFER->offset[0]  = 0;
    PBUFFER->fd[0]      = anonymous_shm_open();
    if (PBUFFER->fd[0] == -1) {
        Debug::log(ERR, "buffer fd failed");
        return;
    }

    if (ftruncate(PBUFFER->fd[0], PBUFFER->size[0]) < 0) {
        close(PBUFFER->fd[0]);
        Debug::log(ERR, "buffer ftruncate failed");
        return;
    }

    PBUFFER->wlBuffer =
        import_wl_shm_buffer(PBUFFER->fd[0], (wl_shm_format)wlSHMFromDrmFourcc(PSTREAM->pSession->sharingData.frameInfoSHM.fmt), PSTREAM->pSession->sharingData.frameInfoSHM.w,
                             PSTREAM->pSession->sharingData.frameInfoSHM.h, PSTREAM->pSession->sharingData.frameInfoSHM.stride);
    if (PBUFFER->wlBuffer == NULL) {
        close(PBUFFER->fd[0]);
        Debug::log(ERR, "buffer import failed");
        return;
    }
    //

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
        exit(1);
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
    const auto PSTREAM = streamFromSession(pSession);

    if (!PSTREAM || !PSTREAM->stream)
        return;

    if (!PSTREAM->buffers.empty()) {
        for (auto& b : PSTREAM->buffers) {
            pwStreamRemoveBuffer(PSTREAM, b->pwBuffer);
        }
    }

    pw_stream_flush(PSTREAM->stream, false);
    pw_stream_disconnect(PSTREAM->stream);
    pw_stream_destroy(PSTREAM->stream);

    pSession->sharingData.active = false;

    std::erase_if(m_vStreams, [&](const auto& other) { return other.get() == PSTREAM; });
}

uint32_t CPipewireConnection::buildFormatsFor(spa_pod_builder* b[2], const spa_pod* params[2], CPipewireConnection::SPWStream* stream) {
    uint32_t paramCount = 0;
    uint32_t modCount   = 0;

    if (/*TODO: dmabuf*/ false) {

    } else {
        paramCount = 1;
        params[0]  = build_format(b[0], pwFromDrmFourcc(stream->pSession->sharingData.frameInfoSHM.fmt), stream->pSession->sharingData.frameInfoSHM.w,
                                 stream->pSession->sharingData.frameInfoSHM.h, FRAMERATE /*TODO: FRAMERATE*/, NULL, 0);
    }

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

    Debug::log(TRACE, "[pw] enqueue on {}", (void*)PSTREAM);

    if (!PSTREAM->currentPWBuffer) {
        Debug::log(ERR, "[pipewire] no buffer in enqueue");
        return;
    }

    spa_buffer* spaBuf  = PSTREAM->currentPWBuffer->pwBuffer->buffer;
    bool        corrupt = PSTREAM->pSession->sharingData.status != FRAME_READY;
    if (corrupt)
        Debug::log(TRACE, "[pw] buffer corrupt");

    spa_meta_header* header;
    if ((header = (spa_meta_header*)spa_buffer_find_meta_data(spaBuf, SPA_META_Header, sizeof(*header)))) {
        header->pts        = PSTREAM->pSession->sharingData.tvSec + SPA_NSEC_PER_SEC * PSTREAM->pSession->sharingData.tvNsec;
        header->flags      = corrupt ? SPA_META_HEADER_FLAG_CORRUPTED : 0;
        header->seq        = PSTREAM->seq++;
        header->dts_offset = 0;
        Debug::log(TRACE, "[pw] enqueue: seq {} pts {}", header->seq, header->pts);
    }

    spa_data* datas = spaBuf->datas;

    for (uint32_t plane = 0; plane < spaBuf->n_datas; plane++) {
        datas[plane].chunk->flags = corrupt ? SPA_CHUNK_FLAG_CORRUPTED : SPA_CHUNK_FLAG_NONE;
    }

    pw_stream_queue_buffer(PSTREAM->stream, PSTREAM->currentPWBuffer->pwBuffer);

    PSTREAM->currentPWBuffer = nullptr;
}

void CPipewireConnection::dequeue(CScreencopyPortal::SSession* pSession) {
    const auto PSTREAM = streamFromSession(pSession);

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

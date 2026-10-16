#include "Screencopy.hpp"
#include "../core/PortalManager.hpp"
#include "../helpers/Log.hpp"
#include "../shared/ImageCopyCapture.hpp"
#include "ext-image-capture-source-v1.hpp"
#include "ext-image-copy-capture-v1.hpp"
#include "hyprland-workspace-image-capture-source-v1.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>
#include <libdrm/drm_fourcc.h>
#include <hyprutils/memory/Casts.hpp>

static constexpr uint32_t MAX_RETRIES = 10;

// Only formats supported by both the PipeWire and wl_shm conversion helpers.
// Prefer common 8-bit formats before considering 10-bit formats.
static constexpr uint32_t WORKSPACE_FORMATS[] = {
    DRM_FORMAT_ARGB8888,    DRM_FORMAT_XRGB8888,    DRM_FORMAT_ABGR8888,    DRM_FORMAT_XBGR8888,    DRM_FORMAT_RGBA8888,    DRM_FORMAT_RGBX8888,
    DRM_FORMAT_BGRA8888,    DRM_FORMAT_BGRX8888,    DRM_FORMAT_ARGB2101010, DRM_FORMAT_XRGB2101010, DRM_FORMAT_ABGR2101010, DRM_FORMAT_XBGR2101010,
    DRM_FORMAT_RGBA1010102, DRM_FORMAT_RGBX1010102, DRM_FORMAT_BGRA1010102, DRM_FORMAT_BGRX1010102,
};

static bool workspaceForceSHM() {
    static const auto PFORCESHM = rc<Hyprlang::INT* const*>(g_pPortalManager->m_sConfig.config->getConfigValuePtr("screencopy:force_shm")->getDataStaticPtr());
    return **PFORCESHM != 0;
}

static bool workspaceDeviceMatches(dev_t device) {
    const auto GBM = g_pPortalManager->m_sWaylandConnection.gbmDevice;
    if (!GBM)
        return false;

    const auto                                              FREE_DEVICE       = [](drmDevice* device) { drmFreeDevice(&device); };
    drmDevice*                                              advertised        = nullptr;
    const int                                               ADVERTISED_RESULT = drmGetDeviceFromDevId(device, 0, &advertised);
    const std::unique_ptr<drmDevice, decltype(FREE_DEVICE)> ADVERTISED(advertised, FREE_DEVICE);
    drmDevice*                                              current        = nullptr;
    const int                                               CURRENT_RESULT = drmGetDevice2(gbm_device_get_fd(GBM), 0, &current);
    const std::unique_ptr<drmDevice, decltype(FREE_DEVICE)> CURRENT(current, FREE_DEVICE);

    return ADVERTISED_RESULT == 0 && CURRENT_RESULT == 0 && ADVERTISED && CURRENT && drmDevicesEqual(ADVERTISED.get(), CURRENT.get());
}

static std::vector<uint64_t> workspaceModifiers(const CImageCopyCapture::SConstraints& constraints, uint32_t format) {
    std::vector<uint64_t> modifiers;
    for (const auto& entry : constraints.dmabufFormats) {
        if (entry.format != format)
            continue;

        for (const auto modifier : entry.modifiers) {
            if (std::ranges::find(modifiers, modifier) != modifiers.end() ||
                !std::ranges::any_of(g_pPortalManager->m_vDMABUFMods, [format, modifier](const auto& mod) { return mod.fourcc == format && mod.mod == modifier; }))
                continue;

            if (modifier != DRM_FORMAT_MOD_INVALID) {
                const int PLANES = gbm_device_get_format_modifier_plane_count(g_pPortalManager->m_sWaylandConnection.gbmDevice, format, modifier);
                if (PLANES <= 0 || PLANES > 4)
                    continue;
            }

            modifiers.push_back(modifier);
        }
    }
    return modifiers;
}

static void workspaceFrameFailed(WP<CScreencopyPortal::SSession> session, CImageCopyCapture::eFailure failure) {
    if (!session || !session->sharingData.active)
        return;

    const auto PIPEWIRE         = g_pPortalManager->m_sPortals.screencopy->m_pPipewire.get();
    const auto STREAM           = PIPEWIRE->streamFromSession(session.get());
    session->sharingData.status = FRAME_FAILED;
    if (STREAM && STREAM->currentPWBuffer)
        PIPEWIRE->enqueue(session.get());

    if (!session || !session->sharingData.active)
        return;

    session->sharingData.status = FRAME_NONE;
    if (failure == CImageCopyCapture::FAILURE_STOPPED) {
        session->stopWorkspaceCapture();
        return;
    }

    if (++session->sharingData.copyRetries > MAX_RETRIES) {
        Debug::log(ERR, "[workspace] Capture failed after {} retries", MAX_RETRIES);
        session->stopWorkspaceCapture();
        return;
    }

    Debug::log(LOG, "[workspace] Retrying capture ({}/{})", session->sharingData.copyRetries, MAX_RETRIES);
    if (failure == CImageCopyCapture::FAILURE_BUFFER_CONSTRAINTS || session->sharingData.iccConstraintsChanged)
        session->updateWorkspaceConstraints();
    else
        g_pPortalManager->m_sPortals.screencopy->queueNextShareFrame(session.get());
}

static void waitForWorkspaceBuffers(CScreencopyPortal::SSession& session) {
    auto&      data = session.sharingData;
    const auto NOW  = std::chrono::steady_clock::now();
    if (data.iccBufferWaitStarted == std::chrono::steady_clock::time_point{})
        data.iccBufferWaitStarted = NOW;

    if (NOW - data.iccBufferWaitStarted >= std::chrono::seconds(5)) {
        Debug::log(ERR, "[workspace] Timed out waiting for compatible PipeWire buffers");
        session.stopWorkspaceCapture();
        return;
    }

    const auto PORTAL = g_pPortalManager->m_sPortals.screencopy.get();
    const auto STREAM = PORTAL->m_pPipewire->streamFromSession(&session);
    data.status       = FRAME_FAILED;
    if (STREAM && STREAM->currentPWBuffer)
        PORTAL->m_pPipewire->enqueue(&session);
    data.status = FRAME_NONE;
    PORTAL->queueNextShareFrame(&session);
}

bool CScreencopyPortal::SSession::initWorkspaceCapture() {
    const auto WORKSPACE = selection.workspaceHandle.lock();
    const auto PORTAL    = g_pPortalManager->m_sPortals.screencopy.get();
    if (selection.type != TYPE_WORKSPACE || !WORKSPACE || WORKSPACE->m_removed || !WORKSPACE->m_handle || !WORKSPACE->m_handle->resource() || !PORTAL->m_sState.icc ||
        !PORTAL->m_sState.icc->resource() || !PORTAL->m_sState.workspaceSource || !PORTAL->m_sState.workspaceSource->resource()) {
        Debug::log(ERR, "[workspace] Selected workspace or capture manager is unavailable");
        return false;
    }

    if (sharingData.icc)
        return !sharingData.icc->stopped();

    selection.workspace = WORKSPACE->m_name;

    const auto SOURCE = makeShared<CCExtImageCaptureSourceV1>(
        PORTAL->m_sState.workspaceSource->sendCreateSource(WORKSPACE->m_handle->resource(), HYPRLAND_WORKSPACE_IMAGE_CAPTURE_SOURCE_MANAGER_V1_CAPTURE_MODE_EVERYTHING));
    sharingData.icc = makeShared<CImageCopyCapture>(PORTAL->m_sState.icc, SOURCE, cursorMode == EMBEDDED);
    if (sharingData.icc->stopped()) {
        sharingData.icc.reset();
        return false;
    }

    sharingData.icc->onConstraints = [weak = self]() {
        if (!weak || !weak->sharingData.active)
            return;
        weak->updateWorkspaceConstraints();
    };
    sharingData.icc->onStopped = [weak = self]() {
        if (!weak || !weak->sharingData.active)
            return;
        weak->stopWorkspaceCapture();
    };
    sharingData.icc->onFailed = [weak = self](CImageCopyCapture::eFailure failure) { workspaceFrameFailed(weak, failure); };
    sharingData.icc->onReady  = [weak = self](const CImageCopyCapture::SFrame& frame) {
        if (!weak || !weak->sharingData.active)
            return;

        auto& data         = weak->sharingData;
        data.status        = FRAME_READY;
        data.tvTimestampNs = frame.timestampNs;
        data.tvSec         = frame.timestampNs / 1000000000ULL;
        data.tvNsec        = sc<uint32_t>(frame.timestampNs % 1000000000ULL);
        data.transform     = sc<wl_output_transform>(frame.transform);
        data.damageCount   = 0;
        for (size_t i = 0; i < std::min(frame.damageCount, frame.damage.size()); ++i) {
            const auto& damage              = frame.damage[i];
            const auto  X                   = std::clamp<int64_t>(damage.x, 0, data.frameInfoSHM.w);
            const auto  Y                   = std::clamp<int64_t>(damage.y, 0, data.frameInfoSHM.h);
            const auto  RIGHT               = std::clamp<int64_t>(sc<int64_t>(damage.x) + damage.width, X, data.frameInfoSHM.w);
            const auto  BOTTOM              = std::clamp<int64_t>(sc<int64_t>(damage.y) + damage.height, Y, data.frameInfoSHM.h);
            data.damage[data.damageCount++] = {
                .x = sc<uint32_t>(X),
                .y = sc<uint32_t>(Y),
                .w = sc<uint32_t>(RIGHT - X),
                .h = sc<uint32_t>(BOTTOM - Y),
            };
        }

        const auto PIPEWIRE = g_pPortalManager->m_sPortals.screencopy->m_pPipewire.get();
        const auto STREAM   = PIPEWIRE->streamFromSession(weak.get());
        if (STREAM && STREAM->currentPWBuffer)
            PIPEWIRE->enqueue(weak.get());

        if (!weak || !weak->sharingData.active)
            return;

        weak->sharingData.copyRetries = 0;
        if (weak->sharingData.iccConstraintsChanged)
            weak->updateWorkspaceConstraints();
        else
            g_pPortalManager->m_sPortals.screencopy->queueNextShareFrame(weak.get());
    };

    return true;
}

void CScreencopyPortal::SSession::updateWorkspaceConstraints() {
    if (!sharingData.active || !sharingData.icc || !sharingData.icc->ready())
        return;

    // Keep the old buffer layout until the outstanding frame has been returned.
    if (sharingData.icc->pending() || sharingData.iccRenegotiating) {
        sharingData.iccConstraintsChanged = true;
        return;
    }

    const auto&        CONSTRAINTS = sharingData.icc->constraints();
    constexpr uint64_t MAX_SIZE    = std::numeric_limits<int32_t>::max();
    if (!CONSTRAINTS.width || !CONSTRAINTS.height || CONSTRAINTS.width > MAX_SIZE || CONSTRAINTS.height > MAX_SIZE) {
        stopWorkspaceCapture();
        return;
    }

    decltype(sharingData.frameInfoSHM) shm = {};
    decltype(sharingData.frameInfoDMA) dma = {};
    shm.w = dma.w = CONSTRAINTS.width;
    shm.h = dma.h = CONSTRAINTS.height;
    shm.fmt = dma.fmt = DRM_FORMAT_INVALID;

    const uint64_t STRIDE = (sc<uint64_t>(CONSTRAINTS.width) * 4 + XDPH_PWR_ALIGN - 1) / XDPH_PWR_ALIGN * XDPH_PWR_ALIGN;
    if (g_pPortalManager->m_sWaylandConnection.shm && STRIDE <= MAX_SIZE && CONSTRAINTS.height <= MAX_SIZE / STRIDE) {
        for (const auto format : WORKSPACE_FORMATS) {
            // ARGB8888/XRGB8888 have special wl_shm values. Never convert an unknown raw enum.
            const auto RAW = sc<uint32_t>(wlSHMFromDrmFourcc(format));
            if (std::ranges::find(CONSTRAINTS.shmFormats, RAW) == CONSTRAINTS.shmFormats.end())
                continue;

            shm.fmt    = format;
            shm.stride = sc<uint32_t>(STRIDE);
            shm.size   = sc<uint32_t>(STRIDE * CONSTRAINTS.height);
            break;
        }
    }

    std::vector<uint64_t> modifiers;
    if (g_pPortalManager->m_sWaylandConnection.linuxDmabuf && CONSTRAINTS.device && workspaceDeviceMatches(*CONSTRAINTS.device)) {
        for (const auto format : WORKSPACE_FORMATS) {
            modifiers = workspaceModifiers(CONSTRAINTS, format);
            if (modifiers.empty())
                continue;

            dma.fmt = format;
            break;
        }
    }

    const bool DMA_CHANGED =
        dma.w != sharingData.frameInfoDMA.w || dma.h != sharingData.frameInfoDMA.h || dma.fmt != sharingData.frameInfoDMA.fmt || modifiers != sharingData.iccModifiers;
    sharingData.frameInfoSHM          = shm;
    sharingData.frameInfoDMA          = dma;
    sharingData.iccModifiers          = std::move(modifiers);
    sharingData.iccConstraintsChanged = false;
    sharingData.iccBufferWaitStarted  = {};

    const auto PIPEWIRE = g_pPortalManager->m_sPortals.screencopy->m_pPipewire.get();
    auto       stream   = PIPEWIRE->streamFromSession(this);
    if (stream && DMA_CHANGED) {
        stream->dmaBufFailed  = false;
        stream->dmaBufRetries = 0;
    }

    if (shm.fmt == DRM_FORMAT_INVALID && (dma.fmt == DRM_FORMAT_INVALID || workspaceForceSHM() || (stream && stream->dmaBufFailed))) {
        Debug::log(ERR, "[workspace] No usable capture buffer format");
        stopWorkspaceCapture();
        return;
    }

    // Initial constraints are consumed by startSharing when it creates the stream.
    if (!stream || !stream->stream)
        return;

    const auto WEAK              = self;
    sharingData.iccRenegotiating = true;
    PIPEWIRE->removeSessionFrameCallbacks(this);
    sharingData.status = FRAME_RENEG;
    if (stream->currentPWBuffer)
        PIPEWIRE->enqueue(this);

    if (!WEAK)
        return;

    stream = PIPEWIRE->streamFromSession(WEAK.get());
    if (WEAK->sharingData.active && stream && stream->stream)
        PIPEWIRE->updateStreamParam(stream);

    if (!WEAK)
        return;

    WEAK->sharingData.iccRenegotiating = false;
    WEAK->sharingData.status           = FRAME_NONE;
    stream                             = PIPEWIRE->streamFromSession(WEAK.get());
    if (WEAK->sharingData.active && stream && stream->stream)
        g_pPortalManager->m_sPortals.screencopy->queueNextShareFrame(WEAK.get());
}

void CScreencopyPortal::SSession::startWorkspaceCopy() {
    if (!sharingData.active || !sharingData.icc || !sharingData.icc->ready() || sharingData.icc->pending() || sharingData.iccRenegotiating)
        return;

    if (sharingData.iccConstraintsChanged) {
        updateWorkspaceConstraints();
        return;
    }

    const auto PORTAL   = g_pPortalManager->m_sPortals.screencopy.get();
    const auto PIPEWIRE = PORTAL->m_pPipewire.get();
    auto       stream   = PIPEWIRE->streamFromSession(this);
    if (!stream || !stream->stream || !stream->streamState)
        return;

    const auto FORMAT    = stream->isDMA ? sharingData.frameInfoDMA.fmt : sharingData.frameInfoSHM.fmt;
    const auto WIDTH     = stream->isDMA ? sharingData.frameInfoDMA.w : sharingData.frameInfoSHM.w;
    const auto HEIGHT    = stream->isDMA ? sharingData.frameInfoDMA.h : sharingData.frameInfoSHM.h;
    const auto PW_FORMAT = FORMAT == DRM_FORMAT_INVALID ? SPA_VIDEO_FORMAT_UNKNOWN : pwFromDrmFourcc(FORMAT);
    const bool FORMAT_MATCHES =
        stream->pwVideoInfo.format == PW_FORMAT || (!stream->isDMA && pwStripAlpha(PW_FORMAT) != SPA_VIDEO_FORMAT_UNKNOWN && stream->pwVideoInfo.format == pwStripAlpha(PW_FORMAT));
    const bool MODIFIER_MATCHES = !stream->isDMA ||
        (!workspaceForceSHM() && !stream->dmaBufFailed && std::ranges::find(sharingData.iccModifiers, stream->pwVideoInfo.modifier) != sharingData.iccModifiers.end());
    if (FORMAT == DRM_FORMAT_INVALID || !FORMAT_MATCHES || !MODIFIER_MATCHES || stream->pwVideoInfo.size.width != WIDTH || stream->pwVideoInfo.size.height != HEIGHT) {
        // EnumFormat updates are asynchronous. Waiting for the consumer is not a failed capture.
        waitForWorkspaceBuffers(*this);
        return;
    }

    const auto WEAK = self;
    if (!stream->currentPWBuffer)
        PIPEWIRE->dequeue(this);

    if (!WEAK || !WEAK->sharingData.active)
        return;

    stream = PIPEWIRE->streamFromSession(this);
    if (!stream || !stream->stream || !stream->streamState)
        return;

    const auto BUFFER = stream->currentPWBuffer;
    if (!BUFFER) {
        // Buffer starvation is not a failed capture, and must not consume retries.
        PORTAL->queueNextShareFrame(this);
        return;
    }

    bool compatible = BUFFER->isDMABUF == stream->isDMA && BUFFER->w == WIDTH && BUFFER->h == HEIGHT && BUFFER->fmt == FORMAT && BUFFER->wlBuffer && BUFFER->wlBuffer->resource();
    if (compatible && BUFFER->isDMABUF) {
        compatible = BUFFER->bo && BUFFER->planeCount > 0 && BUFFER->planeCount <= 4;
        if (compatible) {
            const auto MODIFIER = gbm_bo_get_modifier(BUFFER->bo);
            compatible          = std::ranges::find(sharingData.iccModifiers, MODIFIER) != sharingData.iccModifiers.end() &&
                (stream->pwVideoInfo.modifier == DRM_FORMAT_MOD_INVALID || stream->pwVideoInfo.modifier == MODIFIER);
        }
    } else if (compatible)
        compatible = BUFFER->planeCount == 1 && BUFFER->stride[0] == sharingData.frameInfoSHM.stride && BUFFER->size[0] >= sharingData.frameInfoSHM.size;

    if (!compatible) {
        waitForWorkspaceBuffers(*this);
        return;
    }

    sharingData.iccBufferWaitStarted = {};
    sharingData.damageCount          = 0;
    sharingData.status               = FRAME_QUEUED;
    if (!sharingData.icc->capture(BUFFER->wlBuffer))
        workspaceFrameFailed(self, CImageCopyCapture::FAILURE_UNKNOWN);
}

void CScreencopyPortal::SSession::stopWorkspaceCapture() {
    Debug::log(LOG, "[workspace] Stopping capture of {}", selection.workspace);
    g_pPortalManager->m_sPortals.screencopy->m_pPipewire->destroyStream(this);

    if (!sharingData.started)
        return;

    sharingData.started = false;
    if (session && session->object)
        session->object->emitSignal("Closed").onInterface("org.freedesktop.impl.portal.Session");
}

#include "ImageCopyCapture.hpp"
#include "ext-image-copy-capture-v1.hpp"
#include "ext-image-capture-source-v1.hpp"
#include "wayland.hpp"

#include <cstring>
#include <limits>
#include <utility>
#include <hyprutils/memory/Casts.hpp>

CImageCopyCapture::CImageCopyCapture(SP<CCExtImageCopyCaptureManagerV1> manager, SP<CCExtImageCaptureSourceV1> source, bool paintCursors) : m_source(std::move(source)) {
    if (!manager || !manager->resource() || !m_source || !m_source->resource()) {
        stop();
        return;
    }

    m_session = makeShared<CCExtImageCopyCaptureSessionV1>(
        manager->sendCreateSession(m_source->resource(), paintCursors ? EXT_IMAGE_COPY_CAPTURE_MANAGER_V1_OPTIONS_PAINT_CURSORS : sc<extImageCopyCaptureManagerV1Options>(0)));
    if (!m_session->resource()) {
        stop();
        return;
    }

    m_session->setBufferSize([this](CCExtImageCopyCaptureSessionV1*, uint32_t width, uint32_t height) {
        m_pendingConstraints.width  = width;
        m_pendingConstraints.height = height;
        if (!width || !height || width > std::numeric_limits<int32_t>::max() || height > std::numeric_limits<int32_t>::max())
            m_invalidConstraints = true;
    });
    m_session->setShmFormat([this](CCExtImageCopyCaptureSessionV1*, uint32_t format) { m_pendingConstraints.shmFormats.push_back(format); });
    m_session->setDmabufDevice([this](CCExtImageCopyCaptureSessionV1*, wl_array* device) {
        if (!device || device->size != sizeof(dev_t) || !device->data) {
            m_invalidConstraints = true;
            return;
        }

        dev_t value = 0;
        std::memcpy(&value, device->data, sizeof(value));
        m_pendingConstraints.device = value;
    });
    m_session->setDmabufFormat([this](CCExtImageCopyCaptureSessionV1*, uint32_t format, wl_array* modifiers) {
        if (!modifiers || modifiers->size % sizeof(uint64_t) || (modifiers->size && !modifiers->data)) {
            m_invalidConstraints = true;
            return;
        }

        SDmabufFormat entry;
        entry.format = format;
        entry.modifiers.resize(modifiers->size / sizeof(uint64_t));
        if (modifiers->size)
            std::memcpy(entry.modifiers.data(), modifiers->data, modifiers->size);
        m_pendingConstraints.dmabufFormats.push_back(std::move(entry));
    });
    m_session->setDone([this](CCExtImageCopyCaptureSessionV1*) {
        // The consumer may destroy this helper, including the executing callback's owner.
        const auto SESSION = m_session;
        if (m_invalidConstraints || !m_pendingConstraints.width || !m_pendingConstraints.height) {
            const auto CALLBACK = onStopped;
            stop();
            if (CALLBACK)
                CALLBACK();
            return;
        }

        m_constraints        = std::move(m_pendingConstraints);
        m_pendingConstraints = {};
        m_invalidConstraints = false;
        m_ready              = true;
        const auto CALLBACK  = onConstraints;
        if (CALLBACK)
            CALLBACK();
    });
    m_session->setStopped([this](CCExtImageCopyCaptureSessionV1*) {
        const auto SESSION  = m_session;
        const auto CALLBACK = onStopped;
        stop();
        if (CALLBACK)
            CALLBACK();
    });
}

CImageCopyCapture::~CImageCopyCapture() {
    stop();
}

const CImageCopyCapture::SConstraints& CImageCopyCapture::constraints() const {
    return m_constraints;
}

bool CImageCopyCapture::ready() const {
    return m_ready && !m_stopped;
}

bool CImageCopyCapture::pending() const {
    return !!m_frame;
}

bool CImageCopyCapture::stopped() const {
    return m_stopped;
}

bool CImageCopyCapture::capture(SP<CCWlBuffer> buffer) {
    if (!ready() || pending() || !buffer || !buffer->resource())
        return false;

    m_frame = makeShared<CCExtImageCopyCaptureFrameV1>(m_session->sendCreateFrame());
    if (!m_frame->resource()) {
        cancelFrame();
        return false;
    }

    m_buffer          = std::move(buffer);
    m_frameData       = {};
    m_damageOverflow  = false;
    const auto WIDTH  = sc<int32_t>(m_constraints.width);
    const auto HEIGHT = sc<int32_t>(m_constraints.height);

    m_frame->setTransform([this](CCExtImageCopyCaptureFrameV1*, uint32_t transform) { m_frameData.transform = transform; });
    m_frame->setDamage([this, WIDTH, HEIGHT](CCExtImageCopyCaptureFrameV1*, int32_t x, int32_t y, int32_t width, int32_t height) {
        if (m_damageOverflow)
            return;

        if (m_frameData.damageCount == m_frameData.damage.size()) {
            m_frameData.damage    = {};
            m_frameData.damage[0] = {
                .width  = WIDTH,
                .height = HEIGHT,
            };
            m_frameData.damageCount = 1;
            m_damageOverflow        = true;
            return;
        }

        m_frameData.damage[m_frameData.damageCount++] = {
            .x      = x,
            .y      = y,
            .width  = width,
            .height = height,
        };
    });
    m_frame->setPresentationTime([this](CCExtImageCopyCaptureFrameV1*, uint32_t secHi, uint32_t secLo, uint32_t nsec) {
        m_frameData.timestampNs = ((sc<uint64_t>(secHi) << 32) | secLo) * 1000000000ULL + nsec;
    });
    m_frame->setReady([this](CCExtImageCopyCaptureFrameV1*) {
        const auto FRAME    = std::move(m_frame);
        const auto DATA     = m_frameData;
        const auto CALLBACK = onReady;
        // Destroy the protocol object before allowing the consumer to request another frame.
        FRAME->sendDestroy();
        cancelFrame();
        if (CALLBACK)
            CALLBACK(DATA);
    });
    m_frame->setFailed([this](CCExtImageCopyCaptureFrameV1*, extImageCopyCaptureFrameV1FailureReason reason) {
        const auto FRAME    = std::move(m_frame);
        const auto CALLBACK = onFailed;
        FRAME->sendDestroy();
        cancelFrame();

        eFailure failure = FAILURE_UNKNOWN;
        switch (reason) {
            case EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_BUFFER_CONSTRAINTS: failure = FAILURE_BUFFER_CONSTRAINTS; break;
            case EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_STOPPED:
                failure = FAILURE_STOPPED;
                stop();
                break;
            default: break;
        }

        if (CALLBACK)
            CALLBACK(failure);
    });

    m_frame->sendAttachBuffer(m_buffer->resource());
    m_frame->sendDamageBuffer(0, 0, WIDTH, HEIGHT);
    m_frame->sendCapture();
    return true;
}

void CImageCopyCapture::cancelFrame() {
    m_frame.reset();
    m_buffer.reset();
    m_frameData      = {};
    m_damageOverflow = false;
}

void CImageCopyCapture::stop() {
    m_stopped = true;
    cancelFrame();
    if (m_session) {
        m_session->sendDestroy();
        m_session.reset();
    }
    if (m_source) {
        m_source->sendDestroy();
        m_source.reset();
    }
    m_constraints        = {};
    m_pendingConstraints = {};
    m_ready              = false;
    m_invalidConstraints = false;
}
